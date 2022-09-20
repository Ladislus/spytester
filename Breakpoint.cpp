//
// Created by baptiste on 14/09/22.
//

#include <iostream>
#include <sys/user.h>
#include <sys/ptrace.h>
#include <cstring>
#include <unistd.h>
#include "Breakpoint.h"

BreakPoint::BreakPoint(Tracer& tracer, const std::string &&name, void *addr) :
_addr((uint64_t *)addr), _name(name), _isSet(false), _tracer(tracer),
_setCommand(*this, &BreakPoint::set), _unsetCommand(*this, &BreakPoint::unset){}


void BreakPoint::set()
{
    if(!_isSet)
    {
        if(getpid() == _tracer.getPid()) {
            _backup = *_addr;
            uint64_t newWord = (_backup & (~0xFF)) | INT3;

            if (ptrace(PTRACE_POKEDATA, _tracer.getMainPid(), _addr, newWord) == -1) {
                std::cerr << __FUNCTION__ << " : PTRACE_POKEDATA failed : " << strerror(errno) << std::endl;
            } else {
                std::cout << __FUNCTION__ << " : breakpoint (" << _name << ") set" << std::endl;
                _isSet = true;
            }
        }
        else{
            _tracer.command(&_setCommand);
        }
    }
}

void BreakPoint::unset()
{
    if(_isSet)
    {
        if(getpid() == _tracer.getPid()) {
            if (ptrace(PTRACE_POKEDATA, _tracer.getMainPid(), _addr, _backup) == -1) {
                std::cerr << __FUNCTION__ << " : PTRACE_POKEDATA failed : " << strerror(errno) << std::endl;
            } else {
                std::cout << __FUNCTION__ << " : breakpoint (" << _name << ") unset" << std::endl;
                _isSet = false;
            }
        }
        else{
            _tracer.command(&_unsetCommand);
        }
    }
}

void BreakPoint::restart(SpiedThread &spiedThread) {
    if(!spiedThread.isRunning()){
        unset();

        struct user_regs_struct regs;
        if(ptrace(PTRACE_GETREGS, spiedThread.getPid(), NULL, &regs) == -1)
        {
            std::cerr << __FUNCTION__ <<": PTRACE_GETREGS failed for thread "
                      << spiedThread.getPid() << std::endl;
            return;
        }
        regs.rip--;
        if(ptrace(PTRACE_SETREGS, spiedThread.getPid(), NULL, &regs) == -1)
        {
            std::cerr << __FUNCTION__ <<": PTRACE_SETREGS failed for thread "
                      << spiedThread.getPid() << std::endl;
            return;
        }

        spiedThread.resume();

    }
    else{
        std::cerr << __FUNCTION__ << " : expect stopped thread" <<std::endl;
    }

}

void BreakPoint::hit(SpiedThread &spiedThread) {
    std::cout << __FUNCTION__ << " : thread "<<spiedThread.getPid()<< " hit breakpoint"
    << _name << " at 0x" << std::hex << _addr << std::dec << std::endl;

    restart(spiedThread);
}

