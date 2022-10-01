#include "SpiedThread.h"
#include "Tracer.h"
#include "Breakpoint.h"
#include "SpiedProgram.h"

#include <sys/ptrace.h>

#include <iostream>
#include <cstring>
#include <sys/user.h>
#include <dlfcn.h>

SpiedThread::SpiedThread(SpiedProgram& spiedProgram, Tracer& tracer, pid_t tid) : _tracer(tracer), _tid(tid), _isRunning(false),
_isSigTrapExpected(false), _spiedProgram(spiedProgram)
{}

SpiedThread::~SpiedThread() {
    std::cout << __FUNCTION__ << " : " << _tid << std::endl;
}

pid_t SpiedThread::getTid() const {
    return _tid;
}

bool SpiedThread::isRunning(){
    bool isRunning;

    _isRunningMutex.lock();
    isRunning = _isRunning;
    _isRunningMutex.unlock();

    return isRunning;
}

void SpiedThread::setRunning(bool isRunning) {
    _isRunningMutex.lock();
    _isRunning = isRunning;
    _isRunningMutex.unlock();
    _isRunningCV.notify_all();
}

void SpiedThread::resume(int signum) {
    if(!isRunning())
    {
        if(_tracer.isTracerThread())
        {
            if(ptrace(PTRACE_CONT, _tid, nullptr, signum) == -1) {
                std::cerr << __FUNCTION__ << " : PTRACE_CONT failed for " << _tid << " : " << strerror(errno) << std::endl;
            }
        }
        else
        {
            _tracer.command(std::make_unique<ResumeCmd>(*this, &SpiedThread::resume, signum));
        }
    }
}

void SpiedThread::singleStep() {
    if(!isRunning())
    {
        if(_tracer.isTracerThread())
        {
            std::unique_lock lk(_isRunningMutex);
            if(ptrace(PTRACE_SINGLESTEP, _tid, nullptr, nullptr) == -1) {
                std::cerr << __FUNCTION__ << " : PTRACE_SINGLESTEP failed for " << _tid << " : " << strerror(errno) << std::endl;
                return;
            }
            else
            {
                _isSigTrapExpected = true;
                _isRunning = true;
            }

            // Wait for spied thread to be stopped after single step
            _isRunningCV.wait(lk, [this]{return !_isRunning;} );
        }
        else
        {
            _tracer.command(std::make_unique<SpiedThreadCmd>(*this, &SpiedThread::singleStep));
        }
    }

}

void SpiedThread::stop() {
    if(isRunning()) {
        if(_tracer.isTracerThread())
        {
            if (kill(_tid, SIGSTOP) == -1) {
                std::cerr << __FUNCTION__ << " : SIGSTOP failed for " << _tid << " : " << strerror(errno) << std::endl;
            }
        }
        else
        {
            _tracer.command(std::make_unique<SpiedThreadCmd>(*this, &SpiedThread::stop));
        }
    }
}

void SpiedThread::terminate() {
    if(_tracer.isTracerThread())
    {
        if (kill(_tid, SIGTERM) == -1) {
            std::cerr << __FUNCTION__ << " : SIGTERM failed for " << _tid << " : " << strerror(errno) << std::endl;
        }
    }
    else
    {
        _tracer.command(std::make_unique<SpiedThreadCmd>(*this, &SpiedThread::terminate));
    }
}

void SpiedThread::handleSigTrap() {
    if(!_isSigTrapExpected) {
        struct user_regs_struct regs;

        if (_tracer.isTracerThread()) {
            if (ptrace(PTRACE_GETREGS, _tid, NULL, &regs)) {
                std::cout << __FUNCTION__ << " : PTRACE_GETREGS failed : " << strerror(errno) << std::endl;
                return;
            }

            BreakPoint *const bp = _spiedProgram.getBreakPointAt((void*)(regs.rip - 1));
            if (bp != nullptr) {
                bp->hit(*this);
            } else {
                Dl_info info;
                if (dladdr((void *) (regs.rip - 1), &info) == 0) {
                    std::cerr << __FUNCTION__ << " : unexpected SIGTRAP for thread " << _tid << std::endl;
                } else {
                    std::cout << __FUNCTION__ << " : SIGTRAP at " << info.dli_sname << " ("<< info.dli_fname
                              <<") for "<< _tid << std::endl;
                }
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

