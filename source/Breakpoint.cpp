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

void BreakPoint::set()
{
    if(!_isSet)
    {
        if(_tracer.isTracerThread()) {
            _backup = *_addr;
            uint64_t newWord = (_backup & (~0xFF)) | INT3;

            if (ptrace(PTRACE_POKEDATA, _tracer.getTraceePid(), _addr, newWord) == -1) {
                std::cerr << __FUNCTION__ << " : PTRACE_POKEDATA failed : " << strerror(errno) << std::endl;
            } else {
                std::cout << __FUNCTION__ << " : breakpoint (" << _name << ") set" << std::endl;
                _isSet = true;
            }
        }
        else{
            _tracer.command(Tracer::make_unique_cmd([this]{set();}));
        }
    }
}


void BreakPoint::unset(SpiedThread& sp)
{
    if(_isSet)
    {
        if(_tracer.isTracerThread()) {
            if (ptrace(PTRACE_POKEDATA, sp.getTid(), _addr, _backup) == -1) {
                std::cerr << __FUNCTION__ << " : PTRACE_POKEDATA failed : " << strerror(errno) << std::endl;
            } else {
                std::cout << __FUNCTION__ << " : breakpoint (" << _name << ") unset" << std::endl;
                _isSet = false;
            }
        }
        else{
            _tracer.command(Tracer::make_unique_cmd([this, &sp]{unset(sp);}));
        }
    }
}


void BreakPoint::prepareToResume(SpiedThread& spiedThread)
{
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, spiedThread.getTid(), NULL, &regs) == -1) {
        std::cerr << __FUNCTION__ << ": PTRACE_GETREGS failed for thread "
                  << spiedThread.getTid() << std::endl;
        return;
    }
    regs.rip--;
    if (ptrace(PTRACE_SETREGS, spiedThread.getTid(), NULL, &regs) == -1) {
        std::cerr << __FUNCTION__ << ": PTRACE_SETREGS failed for thread "
                  << spiedThread.getTid() << std::endl;
        return;
    }
}

void BreakPoint::resumeAndSet(SpiedThread &spiedThread)
{
    if(spiedThread.getState() == SpiedThread::STOPPED){
        if(_tracer.isTracerThread())
        {
            prepareToResume(spiedThread);
            unset(spiedThread);
            spiedThread.singleStep();
            set();
            spiedThread.resume();
        }
        else
        {
            _tracer.command(Tracer::make_unique_cmd( [this, &spiedThread]{
                resumeAndSet(spiedThread);
            }));
        }
    }
    else{
        std::cerr << __FUNCTION__ << " : expect stopped thread" <<std::endl;
    }
}


void BreakPoint::resumeAndUnset(SpiedThread &spiedThread) {
    if(spiedThread.getState() == SpiedThread::STOPPED){
        if(_tracer.isTracerThread()) {
            prepareToResume(spiedThread);
            unset(spiedThread);
            spiedThread.resume();
        }
        else
        {
            _tracer.command(Tracer::make_unique_cmd( [this, &spiedThread]{
                resumeAndUnset(spiedThread);
            }));
        }
    }
    else{
        std::cerr << __FUNCTION__ << " : expect stopped thread" <<std::endl;
    }
}

void BreakPoint::setOnHitCallback(std::function<void (BreakPoint&, SpiedThread&)>&& callback) {
    _onHit = callback;
}

void BreakPoint::defaultOnHit(BreakPoint& breakPoint, SpiedThread& spiedThread) {
    std::cout << __FUNCTION__ << " : thread " << spiedThread.getTid() << " hit breakpoint "
              << breakPoint._name << " at 0x" << std::hex << breakPoint._addr << std::dec << std::endl;
}

void BreakPoint::hit(SpiedThread &spiedThread) {
    defaultOnHit(*this, spiedThread);
    _onHit(*this, spiedThread);
}

