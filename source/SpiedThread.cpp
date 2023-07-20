#include <chrono>
#include <dlfcn.h>
#include <iostream>
#include <sys/ptrace.h>
#include <sys/user.h>

#include "SpiedThread.h"
#include "Tracer.h"
#include "Logger.h"

#define STATE_TIMEOUT std::chrono::seconds(5)

SpiedThread::SpiedThread(Tracer &tracer, CallbackHandler &callbackHandler, pid_t tid) :
_tracer(tracer), _callbackHandler(callbackHandler), _tid(tid), _state(STOPPED),
_isSigTrapExpected(false), _regs{}, _regSync(OLD), _dr6{}
{
    for(uint32_t idx = 0; idx<WatchPoint::maxNb; idx++) {
        _watchPoints.emplace_back(std::make_unique<WatchPoint>(_tracer, _callbackHandler, *this, idx),
                                  false);
    }
}

SpiedThread::~SpiedThread() {
    info_log(_tid);
}

pid_t SpiedThread::getTid() const {
    return _tid;
}

void SpiedThread::setState(E_State state) {
    _stateMutex.lock();

    _regSync = OLD;
    _state = state;

    _stateMutex.unlock();

    _stateCV.notify_all();
}

bool SpiedThread::resume(int signum) {
    bool success = true;
    writeRegisters();

    auto res = _tracer.commandPTrace(PTRACE_CONT, _tid, nullptr, signum);
    setState(CONTINUED);
    info_log("Thread (" << _tid << ") resumed");

    return success;
}

bool SpiedThread::singleStep() {
    std::unique_lock stateLk(_stateMutex);
    bool success = true;

    writeRegisters();

    _isSigTrapExpected = true;
    _tracer.commandPTrace(PTRACE_SINGLESTEP, _tid, nullptr, nullptr).wait();

    if (!_stateCV.wait_for(stateLk, STATE_TIMEOUT, [this] { return _state == STOPPED; })) {
        success = false;
        _isSigTrapExpected = false;
        error_log("Timeout for thread " << _tid);
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
            error_log("Timeout for " << _tid);
        }
    }

    return success;
}

bool SpiedThread::terminate() {
    std::unique_lock lk(_stateMutex);
    bool success = true;

    if (_state != TERMINATED && _state != EXITED) {
        if(_state == STOPPED){
            resume();
        }

        _tracer.tkill(_tid, SIGTERM);

        if (!_stateCV.wait_for(lk, STATE_TIMEOUT, [this] { return _state == STOPPED; })) {
            error_log("STOPPED timeout for " << _tid);
            return false;
        }

        resume(SIGTERM);

        if(!_stateCV.wait_for(lk, STATE_TIMEOUT, [this] { return (_state == TERMINATED) || (_state == EXITED);})) {
            error_log("TERMINATE timeout for " << _tid);
            return false;
        }
    }

    return success;
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
        info_log("Thread " << _tid << " at " << (void *)rip);
    } else {
        if (info.dli_sname != nullptr) {
            info_log("Thread " << _tid << " at " << info.dli_sname << " (" << info.dli_fname << ")");
        } else {
            info_log("Thread " << _tid << " at " << (void *) rip << " (" << info.dli_fname << ")");
            return true;
        }
    }

    // Print call stack
    auto rbp = (uint64_t *) getRbp();
    while (rbp) {
        uint64_t retAddr = *(rbp + 1);
        rbp = (uint64_t *) (*rbp);

        if (!dladdr((void *) retAddr, &info)) break;

        if (info.dli_sname) {
            info_log("\tfrom " << info.dli_sname << " (" << info.dli_fname << ")");
        }
        else {
            info_log("\tfrom ?? (" << (void *) retAddr << ") (" << info.dli_fname << ")");
        }
    }
    return true;
}

bool SpiedThread::detach() {
    auto futureRes = _tracer.commandPTrace(PTRACE_DETACH, (_tid-2), 0, SIGSTOP);
    auto res = futureRes.get();
    if(res.first != 0){
        error_log("PTRACE_DETACH failed " << strerror(res.second));
    }

    return (res.first == 0);
}

uint64_t SpiedThread::getRip() {
    readRegisters();

    if(_futureRegs.valid()){
        _futureRegs.get();
    }

    return _regs.rip;
}

uint64_t SpiedThread::getRbp() {
    readRegisters();

    if(_futureRegs.valid()){
        _futureRegs.get();
    }

    return _regs.rbp;
}

uint64_t SpiedThread::getDr6() {
    readRegisters();

    if(_futureDr6.valid()){
        _dr6 = _futureDr6.get().first;
    }

    return _dr6;
}

void SpiedThread::jump(void* addr) {
    std::lock_guard lk(_stateMutex);
    readRegisters();

    if(_futureRegs.valid()){
        _futureRegs.get();
    }

    _regs.rip = (uint64_t)addr;
    _regSync = NEW;
}

void SpiedThread::setDr6(uint64_t dr6){
    std::lock_guard lk(_stateMutex);
    readRegisters();

    if(_futureDr6.valid()){
        _futureDr6.get();
    }

    _dr6 = (uint64_t)dr6;
    _regSync = NEW;
}

void SpiedThread::writeRegisters() {
    std::lock_guard lk(_stateMutex);

    if(_regSync == NEW){
        // If the register values have not yet been written to SpiedThread attributes,
        // no need to write them back in registers
        if(!_futureRegs.valid()){
            _tracer.commandPTrace(PTRACE_SETREGS, _tid, NULL, &_regs);}
        if(!_futureDr6.valid()) {
            _tracer.commandPTrace(PTRACE_POKEUSER, _tid, offsetof(struct user, u_debugreg[6]), _dr6);
        }
        _regSync = SYNC;
    }
}

void SpiedThread::readRegisters() {
    std::lock_guard lk(_stateMutex);

    if(_state != STOPPED) return;

    if(_regSync == OLD){
        _futureDr6 = _tracer.commandPTrace(PTRACE_PEEKUSER, _tid, offsetof(struct user, u_debugreg[6]), NULL);
        _futureRegs = _tracer.commandPTrace(PTRACE_GETREGS, _tid, NULL, &_regs);

        _regSync = SYNC;
    }
}

bool SpiedThread::handleEvent(SpiedThread::E_State state, int signal, int status, uint16_t ptraceEvent) {
    bool isEventHandled = false;
    readRegisters();

    switch(state){
        case CONTINUED:
            info_log("Thread (" << _tid << ") is running");
            setState(CONTINUED);
            isEventHandled = true;
        break;

        case EXITED:
            info_log("Thread (" << _tid << ") exited with status " << status);
            setState(EXITED);
            isEventHandled = true;
        break;

        case TERMINATED:
            info_log("Thread (" << _tid << ") terminated with signal " << signal);
            setState(TERMINATED);
            isEventHandled = true;
        break;

        case STOPPED:
            setState(STOPPED);
            if(signal == SIGTRAP) {
                if (_isSigTrapExpected) { // #FIXME find a way not to use _isSigTrapExpected
                    _isSigTrapExpected = false;
                    isEventHandled = true;
                }

                if(ptraceEvent == PTRACE_EVENT_CLONE){
                    resume();
                    isEventHandled = true;
                }

                uint64_t dr6 = getDr6();
                for (uint32_t idx = 0; idx < WatchPoint::maxNb; idx++) {
                    if (dr6 & (1 << idx)) {
                        setDr6(dr6 & (~(1 << idx)));

                        _watchPoints[idx].first->hit();
                        isEventHandled = true;
                        break;
                    }
                }
            } else if(signal == SIGSEGV) {
                info_log("SIGSEGV received");
                backtrace();
               /* resume(SIGSTOP);
                detach();
                std::string gdbCommand = "gdb attach " + std::to_string(_tid - 2);
                if(system(gdbCommand.c_str()) != 0){
                    error_log("Gdb attach failed");
                }*/
            } else if(signal == SIGSTOP){
                info_log("SIGSTOP received");
            } else {
                info_log("Thread (" << _tid << ") received signal " << signal);
                resume(signal);
                isEventHandled = true;
            }
        break;

        default:
            break;
    }

    return isEventHandled;
}

