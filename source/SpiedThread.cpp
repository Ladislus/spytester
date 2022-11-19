#include "../include/SpiedThread.h"
#include "../include/Tracer.h"
#include "../include/Breakpoint.h"
#include "../include/SpiedProgram.h"

#include <sys/ptrace.h>
#include <chrono>
#include <iostream>
#include <cstring>
#include <sys/user.h>
#include <dlfcn.h>

#define STATE_TIMEOUT std::chrono::seconds(5)

SpiedThread::SpiedThread(SpiedProgram& spiedProgram, Tracer& tracer, pid_t tid) : _tracer(tracer), _tid(tid),
_state(STOPPED), _isSigTrapExpected(false), _spiedProgram(spiedProgram), _code(0), _isRegsDirty(true),
_regs(), _isRegsModified(false)
{
    for(uint32_t idx = 0; idx<WatchPoint::maxNb; idx++){
        _watchPoints.emplace_back( std::make_pair(
                        std::make_unique<WatchPoint>(_tracer, *this, idx),
                        false));
    }
}

SpiedThread::~SpiedThread() {
    std::cout << __FUNCTION__ << " : " << _tid << std::endl;
}

pid_t SpiedThread::getTid() const {
    return _tid;
}

void SpiedThread::setState(E_State state, int code) {
    _stateMutex.lock();

    _isRegsDirty = true;

    _code = code;
    _state = state;

    _stateMutex.unlock();

    _stateCV.notify_all();
}

bool SpiedThread::resume(int signum) {

    bool success;
    setRegisters();

    if (_tracer.commandPTrace(false, PTRACE_CONT, _tid, nullptr, signum) == -1) {
        success = false;
        std::cerr << "SpiedThread::resume : PTRACE_CONT failed for " << _tid << " : " << strerror(errno)
                  << std::endl;
    } else {
        success = true;
        setState(RUNNING, 0);
        std::cout << "SpiedThread::resume : thread " <<_tid <<" resumed" <<  std::endl;
    }

    return success;
}

bool SpiedThread::singleStep() {
    std::unique_lock stateLk(_stateMutex);

    bool success = true;
    setRegisters();

    _isSigTrapExpected = true;
    if (_tracer.commandPTrace(true, PTRACE_SINGLESTEP, _tid, nullptr, nullptr) == -1) {
        success = false;
        _isSigTrapExpected = false;
        std::cerr << "SpiedThread::singleStep : PTRACE_SINGLESTEP failed for "
                  << _tid << " : " << strerror(errno) << std::endl;
    } else {
        setState(RUNNING);
        if (!_stateCV.wait_for(stateLk, STATE_TIMEOUT, [this] { return _state == STOPPED; })) {
            success = false;
            _isSigTrapExpected = false;
            std::cerr << "SpiedThread::singleStep timeout for thread " << _tid << std::endl;
        }
    }

    return success;
}

bool SpiedThread::stop() {

    bool success = true;
    std::unique_lock lk(_stateMutex);

    if(_state != STOPPED) {
        _tracer.tkill(_tid, SIGSTOP);
        if (!_stateCV.wait_for(lk, STATE_TIMEOUT, [this] { return _state == STOPPED; })) {
            success = false;
            std::cerr << "SpiedThread::stop timeout for " << _tid << std::endl;
        }
    }

    return success;
}

bool SpiedThread::terminate() {
    std::unique_lock lk(_stateMutex);
    bool success = true;

    if (_state != TERMINATED) {
        if (_state != STOPPED) {
            _tracer.tkill(_tid, SIGTERM);
            if(!_stateCV.wait_for(lk, STATE_TIMEOUT, [this] { return _state == STOPPED;})) {
                std::cerr << "SpiedThread::terminate STOP timeout for " << _tid << std::endl;
                return false;
            }
        }

        _tracer.commandPTrace(false, PTRACE_CONT, _tid, nullptr ,SIGTERM);

        if(!_stateCV.wait_for(lk, STATE_TIMEOUT, [this] { return _state == TERMINATED;})) {
            std::cerr << "SpiedThread::terminate TERMINATE timeout for " << _tid << std::endl;
            return false;
        }
    }

    return success;
}

bool SpiedThread::handleSigTrap(int wstatus) {
    uint64_t rip;

    if(_isSigTrapExpected){
        _isSigTrapExpected = false;
        return true;
    }

    // Handle thread creation
    if ((wstatus >> 16) == PTRACE_EVENT_CLONE) {

        unsigned long newThread;
        _tracer.commandPTrace(true, PTRACE_GETEVENTMSG, _tid, nullptr, &newThread);

        std::cout << "SpiedThread::handleSigTrap : thread " << _tid << " create new thread "
                  << newThread << std::endl;

        resume();
        return true;
    }
    rip = getRip();
    // Handle breakpoint
    BreakPoint *const bp = _spiedProgram.getBreakPointAt((void *) (rip - 1));
    if (bp != nullptr) {
        bp->hit(*this);
        return true;
    }

    // Handle watchpoint
    uint64_t dr6 = _tracer.commandPTrace(true, PTRACE_PEEKUSER, _tid, offsetof(struct user, u_debugreg[6]), NULL);
    if (dr6 == -1) {
        std::cerr << "SpiedThread::handleSigTrap : PTRACE_PEEKUSER failed for thread << "
                  << _tid << " : " << strerror(errno) << std::endl;
        return true;
    }

    uint32_t idx;
    for (idx = 0; idx < WatchPoint::maxNb; idx++) {
        if (dr6 & (1 << idx)) break;
    }

    if (idx < _watchPoints.size()) {
        // Clean dr6 register
        dr6 &= ~(1 << idx);

        if (_tracer.commandPTrace(true, PTRACE_POKEUSER, _tid, offsetof(struct user, u_debugreg[6]), dr6) == -1) {
            std::cerr << "SpiedThread::handleSigTrap : PTRACE_POKEUSER failed for thread << "
                      << _tid << " : " << strerror(errno) << std::endl;
        }

        _watchPoints[idx].first->hit();

        return true;
    }

    // Handle unexpected SIGTRAP
    Dl_info info;
    if (dladdr((void *) (rip - 1), &info) == 0) {
        std::cerr << "SpiedThread::handleSigTrap : unexpected SIGTRAP for thread " << _tid << std::endl;
    } else {
        std::cout << "SpiedThread::handleSigTrap : SIGTRAP at " << info.dli_sname << " (" << info.dli_fname
                  << ") for " << _tid << std::endl;
    }
    resume();

    return false;
}

WatchPoint *SpiedThread::createWatchPoint() {
    WatchPoint* watchPoint = nullptr;

    for(auto& wp : _watchPoints){
        if(!wp.second){
            watchPoint = wp.first.get();
            break;
        }
    }

    return watchPoint;
}

void SpiedThread::deleteWatchPoint(WatchPoint *watchPoint) {
    for (auto & wp : _watchPoints){
        if(watchPoint == wp.first.get()){
            watchPoint->unset();
            wp.second = false;
            break;
        }
    }
}

bool SpiedThread::backtrace() {

    // Get register
    uint64_t rip = getRip();

    // Print thread current position
    Dl_info info;
    if (dladdr((void *) rip, &info) == 0) {
        std::cout << "SpiedThread::backtrace : thread " << _tid << " at " << (void *)rip << std::endl;
    } else {
        if (info.dli_sname != nullptr) {
            std::cout << "SpiedThread::backtrace : thread " << _tid << " at " << info.dli_sname << " ("
                      << info.dli_fname << ")" << std::endl;
        } else {
            std::cout << "thread " << _tid << " at " << (void *) rip
                      << " (" << info.dli_fname << ")" << std::endl;
            return true;
        }
    }

    // Print call stack
    auto rbp = (uint64_t *) getRbp();
    while (rbp != nullptr) {
        uint64_t retAddr = *(rbp + 1);
        rbp = (uint64_t *) (*rbp);

        if (dladdr((void *) retAddr, &info) == 0) {
            break;
        } else {
            if (info.dli_sname != nullptr) {
                std::cout << "\tfrom " << info.dli_sname << " (" << info.dli_fname << ")" << std::endl;
            } else {
                std::cout << "\tfrom ?? (" << (void *) retAddr << ") (" << info.dli_fname << ")" << std::endl;
            }
        }
    }
    return true;
}

bool SpiedThread::detach() {
    bool res = true;
    if (_tracer.commandPTrace(true, PTRACE_DETACH, _tid, 0, 0) == -1) {
        res = false;
        std::cerr << "SpiedThread::detach : PTRACE_DETACH failed for thread " << _tid << " : "
                  << strerror(errno) << std::endl;
    }
    return res;
}

uint64_t SpiedThread::getRip() {
    uint64_t rip = 0U;

    getRegisters();

    if(!_isRegsDirty){
        rip = _regs.rip;
    }
    return rip;
}

uint64_t SpiedThread::getRbp() {
    uint64_t rbp = 0U;

    getRegisters();

    if(!_isRegsDirty){
        rbp = _regs.rbp;
    }
    return rbp;
}

void SpiedThread::setRip(uint64_t rip) {
    std::lock_guard lk(_stateMutex);
    getRegisters();

    if(!_isRegsDirty){
        _isRegsModified = true;
        _regs.rip = rip;
    }
}

void SpiedThread::setRegisters() {
    std::lock_guard lk(_stateMutex);

    if(_isRegsModified){
        if(_tracer.commandPTrace(false, PTRACE_SETREGS, _tid, NULL, &_regs) == -1) {
            std::cerr << __FUNCTION__ << ": PTRACE_SETREGS failed for thread "
                      << _tid << std::endl;
            return;
        }
        _isRegsModified = false;
    }
}

void SpiedThread::getRegisters() {
    std::lock_guard lk(_stateMutex);

    if(_state != STOPPED) return;

    if(_isRegsDirty){
        if (_tracer.commandPTrace(true, PTRACE_GETREGS, _tid, NULL, &_regs) == -1) {
            std::cerr << __FUNCTION__ <<" : PTRACE_GETREGS failed : " << strerror(errno) << std::endl;
            return;
        }
        _isRegsDirty = false;
    }
    _isRegsModified = true;
}

