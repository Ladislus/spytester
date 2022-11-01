#include <iostream>
#include <sys/user.h>
#include <sys/ptrace.h>
#include <cstring>
#include <unistd.h>

#include "../include/Breakpoint.h"

BreakPoint::BreakPoint(Tracer& tracer, const std::string &&name, void *addr) :
_addr((uint64_t *)addr), _name(name), _isSet(false), _tracer(tracer),_onHit(BreakPoint::defaultOnHit){}


void *BreakPoint::getAddr() const{
    return _addr;
}

bool BreakPoint::set(){
    return _tracer.command([this] {
        if (!_isSet) {
            _backup = *_addr;
            uint64_t newWord = (_backup & (~0xFF)) | INT3;

            if (ptrace(PTRACE_POKEDATA, _tracer.getTraceePid(), _addr, newWord) == -1) {
                std::cerr << "BreakPoint::set : PTRACE_POKEDATA failed : " << strerror(errno) << std::endl;
            } else {
                std::cout << "BreakPoint::set : breakpoint (" << _name << ") set" << std::endl;
                _isSet = true;
            }
        }
        return _isSet;
    });
}


bool BreakPoint::unset(){
    return _tracer.command([this] {
        if (_isSet) {
            if (ptrace(PTRACE_POKEDATA, _tracer.getTraceePid(), _addr, _backup) == -1) {
                std::cerr << "BreakPoint::unset : PTRACE_POKEDATA failed : " << strerror(errno) << std::endl;
            } else {
                std::cout << "BreakPoint::unset : breakpoint (" << _name << ") unset" << std::endl;
                _isSet = false;
            }
        }
        return !_isSet;
    });
}


bool BreakPoint::prepareToResume(SpiedThread& spiedThread){
    struct user_regs_struct regs;
    bool res = true;

    if (ptrace(PTRACE_GETREGS, spiedThread.getTid(), NULL, &regs) == -1) {
        std::cerr << __FUNCTION__ << ": PTRACE_GETREGS failed for thread "
                  << spiedThread.getTid() << std::endl;
        res = false;
    }
    regs.rip--;
    if (ptrace(PTRACE_SETREGS, spiedThread.getTid(), NULL, &regs) == -1) {
        std::cerr << __FUNCTION__ << ": PTRACE_SETREGS failed for thread "
                  << spiedThread.getTid() << std::endl;
        res = false;
    }

    return res;
}

bool BreakPoint::resumeAndSet(SpiedThread &spiedThread)
{
    return _tracer.command([this, &spiedThread] {
        return prepareToResume(spiedThread)
               && unset()
               && spiedThread.singleStep()
               && set()
               && spiedThread.resume();
    });
}

bool BreakPoint::resumeAndUnset(SpiedThread &spiedThread) {
    return _tracer.command([this, &spiedThread] {
        return prepareToResume(spiedThread)
               && unset()
               && spiedThread.resume();
    });
}

void BreakPoint::setOnHitCallback(std::function<void (BreakPoint&, SpiedThread&)>&& callback) {
    _breakPointMutex.lock();
    _onHit = callback;
    _breakPointMutex.unlock();
}

void BreakPoint::defaultOnHit(BreakPoint& breakPoint, SpiedThread& spiedThread) {
    std::cout << __FUNCTION__ << " : thread " << spiedThread.getTid() << " hit breakpoint "
              << breakPoint._name << " at 0x" << std::hex << breakPoint._addr << std::dec << std::endl;
}

void BreakPoint::hit(SpiedThread &spiedThread) {
    defaultOnHit(*this, spiedThread);
    _onHit(*this, spiedThread);
}

