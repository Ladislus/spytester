#include "SpiedThread.h"
#include "Tracer.h"
#include "Breakpoint.h"
#include "SpiedProgram.h"

#include <sys/ptrace.h>

#include <iostream>
#include <cstring>
#include <sys/user.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

#define tkill(tid, sig) syscall(SYS_tkill, tid, sig)

SpiedThread::SpiedThread(SpiedProgram& spiedProgram, Tracer& tracer, pid_t tid) : _tracer(tracer), _tid(tid),
_state(STOPPED), _isSigTrapExpected(false), _spiedProgram(spiedProgram)
{
    for(uint32_t idx = 0; idx<WatchPoint::maxNb; idx++){
        _watchPoints.emplace_back(std::make_pair(WatchPoint(_tracer, *this, idx), false));
    }
}

SpiedThread::~SpiedThread() {
    std::cout << __FUNCTION__ << " : " << _tid << std::endl;
}

pid_t SpiedThread::getTid() const {
    return _tid;
}

SpiedThread::E_State SpiedThread::getState(){
    E_State state;

    _stateMutex.lock();
    state = _state;
    _stateMutex.unlock();

    return state;
}

void SpiedThread::setState(E_State state) {
    _stateMutex.lock();
    _state = state;
    _stateMutex.unlock();

    _stateCV.notify_all();
}

void SpiedThread::resume(int signum) {
    if(getState() == STOPPED)
    {
        if(_tracer.isTracerThread())
        {
            if(ptrace(PTRACE_CONT, _tid, nullptr, signum) == -1) {
                std::cerr << __FUNCTION__ << " : PTRACE_CONT failed for " << _tid << " : " << strerror(errno) << std::endl;
            }
            else{
                std::cout << __FUNCTION__ << " : thread "<<_tid << " resumed"<< std::endl;
                setState(RUNNING);
            }
        }
        else
        {
            _tracer.command(std::make_unique<ResumeCmd>(*this, &SpiedThread::resume, signum));
        }
    }
    else
    {
        std::cout << __FUNCTION__ << " : thread "<<_tid << " is already running!"<< std::endl;
    }
}

void SpiedThread::singleStep() {
    if(getState() == STOPPED)
    {
        if(_tracer.isTracerThread())
        {
            std::unique_lock lk(_stateMutex);
            if(ptrace(PTRACE_SINGLESTEP, _tid, nullptr, nullptr) == -1) {
                std::cerr << __FUNCTION__ << " : PTRACE_SINGLESTEP failed for " << _tid << " : " << strerror(errno) << std::endl;
                return;
            }
            else
            {
                _isSigTrapExpected = true;
                _state = RUNNING;
            }

            // Wait for spied thread to be stopped after single step
            _stateCV.wait(lk, [this]{ return _state == STOPPED;} );
        }
        else
        {
            _tracer.command(std::make_unique<SpiedThreadCmd>(*this, &SpiedThread::singleStep));
        }
    }

}

void SpiedThread::stop() {
    if(getState() == RUNNING) {
        if(_tracer.isTracerThread()){

            if (tkill(_tid, SIGSTOP) == -1) {
                std::cerr << __FUNCTION__ << " : SIGSTOP failed for " << _tid << " : " << strerror(errno) << std::endl;
            }
        }
        else
        {
            std::unique_lock lk(_stateMutex);

            _tracer.command(std::make_unique<SpiedThreadCmd>(*this, &SpiedThread::stop));

            // Wait for the spied thread to be actually stopped
            _stateCV.wait(lk, [this]{ return _state == STOPPED;} );
        }
    }
    else{
        std::cout << __FUNCTION__ << " : thread " << _tid << " is already stopped " << std::endl;
    }
}

void SpiedThread::terminate() {
    if(getState() != TERMINATED) {
        if (_tracer.isTracerThread()) {
            if (tkill(_tid, SIGTERM) == -1) {
                std::cerr << __FUNCTION__ << " : SIGTERM failed for " << _tid << " : " << strerror(errno) << std::endl;
            }
        } else {
            std::unique_lock lk(_stateMutex);

            _tracer.command(std::make_unique<SpiedThreadCmd>(*this, &SpiedThread::terminate));

            // Wait for the spied thread to be actually terminated
            _stateCV.wait(lk, [this]{ return _state == TERMINATED;} );
        }
    }
}

void SpiedThread::handleSigTrap() {
    if(!_isSigTrapExpected) {
        struct user_regs_struct regs;

        if (_tracer.isTracerThread()) {

            // Handle breakpoint
            if (ptrace(PTRACE_GETREGS, _tid, NULL, &regs)) {
                std::cout << __FUNCTION__ << " : PTRACE_GETREGS failed : " << strerror(errno) << std::endl;
                return;
            }

            BreakPoint *const bp = _spiedProgram.getBreakPointAt((void*)(regs.rip - 1));
            if (bp != nullptr) {
                bp->hit(*this);
                return;
            }

            // Handle watchpoint
            uint64_t dr6 = ptrace(PTRACE_PEEKUSER, _tid, offsetof(struct user, u_debugreg[6]), NULL);
            if (dr6 == -1)
            {
                std::cerr <<__FUNCTION__ <<" : PTRACE_PEEKUSER failed for thread << "
                << _tid <<" : " << strerror(errno) << std::endl;
                return;
            }

            uint32_t idx;
            for(idx=0; idx<WatchPoint::maxNb; idx++){
                if(dr6 & (1<<idx)) break;
            }

            if(idx < _watchPoints.size()){
                // Clean dr6 register
                dr6 &= ~(1<<idx);

                if (ptrace(PTRACE_POKEUSER, _tid, offsetof(struct user, u_debugreg[6]), dr6) == -1){
                    std::cerr <<__FUNCTION__ <<" : PTRACE_POKEUSER failed for thread << "
                              << _tid <<" : " << strerror(errno) << std::endl;
                }

                WatchPoint& watchPoint = _watchPoints[idx].first;
                watchPoint.hit();

                return;
            }

            // Handle unexpected SIGTRAP
            Dl_info info;
            if (dladdr((void *) (regs.rip - 1), &info) == 0) {
                std::cerr << __FUNCTION__ << " : unexpected SIGTRAP for thread " << _tid << std::endl;
            } else {
                std::cout << __FUNCTION__ << " : SIGTRAP at " << info.dli_sname << " (" << info.dli_fname
                          << ") for " << _tid << std::endl;
            }

        } else {
            _tracer.command(std::make_unique<SpiedThreadCmd>(*this, &SpiedThread::handleSigTrap));
        }
    }
    else
    {
        _isSigTrapExpected = false;
    }
}

WatchPoint *SpiedThread::createWatchPoint() {
    WatchPoint* watchPoint = nullptr;

    for(auto& wp : _watchPoints){
        if(!wp.second){
            watchPoint = &wp.first;
            break;
        }
    }

    return watchPoint;
}

void SpiedThread::deleteWatchPoint(WatchPoint *watchPoint) {
    for (auto & wp : _watchPoints){
        if(watchPoint == &wp.first){
            watchPoint->unset();
            wp.second = false;
            break;
        }
    }
}

