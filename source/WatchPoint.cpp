#include "../include/WatchPoint.h"
#include "../include/Tracer.h"
#include "../include/SpiedThread.h"

#include <sys/ptrace.h>
#include <sys/user.h>
#include <cstring>

void defaultOnHit(WatchPoint& watchPoint, SpiedThread& spiedThread){
    std::cout << __FUNCTION__ << " : thread "<< spiedThread.getTid() << " hits watchpoint at "
    << watchPoint.getAddr()<< std::endl;
}

WatchPoint::WatchPoint(Tracer &tracer, SpiedThread& spiedThread, uint32_t idx) :
_tracer(tracer), _spiedThread(spiedThread), _addr(nullptr), _idx(idx), _isSet(false), _onHit(defaultOnHit)
{}

WatchPoint::~WatchPoint() {
    unset();
}

void WatchPoint::set(void *addr, WatchPoint::E_Trigger trigger, E_Size size) {
    if(_tracer.isTracerThread()) {

        uint64_t dr7 = ptrace(PTRACE_PEEKUSER, _spiedThread.getTid(), offsetof(struct user, u_debugreg[7]), NULL);
        if(dr7 == -1)
        {
            std::cerr << __FUNCTION__ << " : PTRACE_PEEKUSER for dr7 failed : " << strerror(errno);
            return;
        }

        const uint64_t offset =  offsetof(struct user, u_debugreg[0]) + _idx * sizeof(user::u_debugreg[0]);
        if (ptrace(PTRACE_POKEUSER, _spiedThread.getTid(), offset, addr) == -1)
        {
            std::cerr << __FUNCTION__ << " : PTRACE_POKEUSER for dri failed : " << strerror(errno);
            return;
        }
        _addr = addr;

        // Reset dr7 bits corresponding to current watchpoint
        dr7 &= ~((0b11<<(_idx * 2)) | (0b1111 << (_idx * 4 + 16)));

        // Set dr7 bits corresponding to parameters
        dr7 |= (0b11 << (_idx * 2));
        dr7 |= (trigger << (_idx * 4 + 16));
        dr7 |= (size << (_idx * 4 + 18));

        if(ptrace(PTRACE_POKEUSER, _spiedThread.getTid(), offsetof(struct user, u_debugreg[7]), dr7) == -1)
        {
            std::cerr << __FUNCTION__ << " : PTRACE_POKEUSER for dr7 failed : " << strerror(errno);
            return;
        }

        _addr =  addr;
        _isSet = true;
    }
    else {
        _tracer.command(std::make_unique<WatchPointSetCmd>(*this, &WatchPoint::set, addr, trigger, size));
    }
}

void WatchPoint::unset() {
    if(_isSet)
    {
        if(_tracer.isTracerThread()){

            uint64_t dr7 = ptrace(PTRACE_PEEKUSER, _spiedThread.getTid(), offsetof(struct user, u_debugreg[7]), NULL);
            if(dr7 == -1)
            {
                std::cerr << __FUNCTION__ << " : PTRACE_PEEKUSER for dr7 failed : " << strerror(errno);
                return;
            }

            dr7 &= ~(0b11<<(_idx * 2));

            if(ptrace(PTRACE_POKEUSER, _spiedThread.getTid(), offsetof(struct user, u_debugreg[7]), dr7) == -1)
            {
                std::cerr << __FUNCTION__ << " : PTRACE_POKEUSER for dr7 failed : " << strerror(errno);
                return;
            }

            _isSet = false;
        }
        else{
            _tracer.command(std::make_unique<WatchPointUnsetCmd>(*this, &WatchPoint::unset));
        }
    }
}

void WatchPoint::setOnHit(void (*onHit)(WatchPoint &, SpiedThread &)) {
    _onHit = onHit;
}

void WatchPoint::hit() {
    _onHit(*this, _spiedThread);
}

void *WatchPoint::getAddr() {
    return _addr;
}

bool WatchPoint::isSet() const{
    return _isSet;
}

