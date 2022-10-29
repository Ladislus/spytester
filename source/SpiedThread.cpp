#include "../include/SpiedThread.h"
#include "../include/Tracer.h"
#include "../include/Breakpoint.h"
#include "../include/SpiedProgram.h"

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
            setState(RUNNING);
            if(ptrace(PTRACE_CONT, _tid, nullptr, signum) == -1) {
                setState(STOPPED);
                std::cerr << __FUNCTION__ << " : PTRACE_CONT failed for " << _tid << " : " << strerror(errno) << std::endl;
            }
            else{
                std::cout << __FUNCTION__ << " : thread "<<_tid << " resumed"<< std::endl;
            }
        }
        else
        {
            _tracer.command(Tracer::make_unique_cmd([this, signum]{resume(signum);}));
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
            _tracer.command(Tracer::make_unique_cmd([this]{singleStep();}));
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
            _tracer.command(Tracer::make_unique_cmd([this]{stop();}));

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

            _tracer.command(Tracer::make_unique_cmd([this]{terminate();}));

            // Wait for the spied thread to be actually terminated
            _stateCV.wait(lk, [this]{ return _state == TERMINATED;} );
        }
    }
}

void SpiedThread::handleSigTrap(int wstatus) {
    if(!_isSigTrapExpected) {
        struct user_regs_struct regs;

        if (_tracer.isTracerThread()) {

            // Handle thread creation
            if((wstatus >> 16) == PTRACE_EVENT_CLONE){

                unsigned long newThread;
                ptrace(PTRACE_GETEVENTMSG, _tid, nullptr, &newThread);

                std::cout << __FUNCTION__ << " : thread "<<_tid <<" create new thread "
                << newThread << std::endl;

                resume();
                return;
            }

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
            resume();

        } else {
            _tracer.command(Tracer::make_unique_cmd([this, wstatus]{ handleSigTrap(wstatus);}));
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

void SpiedThread::backtrace() {
    if(_tracer.isTracerThread())
    {
        // Get register
        struct user_regs_struct regs;
        if (ptrace(PTRACE_GETREGS, _tid, NULL, &regs) == -1) {
            std::cerr << __FUNCTION__ << ": PTRACE_GETREGS failed for thread "
                      << _tid << std::endl;
            return;
        }

        // Get thread current position
        Dl_info info;
        if (dladdr((void *)regs.rip, &info) == 0) {
            std::cout << __FUNCTION__ << " : thread " << _tid <<" at "<< (void *)regs.rip <<std::endl;
        }
        else{
            if(info.dli_sname != nullptr) {
                std::cout << __FUNCTION__ << " : thread " << _tid << " at " << info.dli_sname << " ("
                          << info.dli_fname << ")" << std::endl;
            }
            else{
                std::cout << "thread " << _tid << " at "<< (void *)regs.rip
                          << " (" << info.dli_fname << ")" << std::endl;
                return;
            }
        }

        // Get call stack
        auto rbp = (uint64_t*)regs.rbp;
        while(rbp != nullptr)
        {
            uint64_t retAddr = *(rbp+1);
            rbp = (uint64_t *)(*rbp);

            if (dladdr((void *)retAddr, &info) == 0) {
                break;
            }
            else {
                if(info.dli_sname != nullptr) {
                    std::cout << "\tfrom " << info.dli_sname << " (" << info.dli_fname << ")" << std::endl;
                }
                else{
                    std::cout << "\tfrom ?? ("<< (void*)retAddr <<") (" << info.dli_fname << ")" << std::endl;
                }
            }
        }
    }
    else
    {
        _tracer.command(Tracer::make_unique_cmd([this]{backtrace();}));
    }
}

void SpiedThread::detach() {
    if(_tracer.isTracerThread()) {
        if(ptrace(PTRACE_DETACH, _tid, 0, 0) == -1){
            std::cerr<< __FUNCTION__ <<" : PTRACE_DETACH failed for thread "<< _tid << " : "
            << strerror(errno) << std::endl;
        }
        else
        {
            std::cout << __FUNCTION__ <<" : thread "<< _tid << " detached" << std::endl;
        }
    }else{
        _tracer.command(Tracer::make_unique_cmd([this]{detach();}));
    }
}

