#include "../include/WatchPoint.h"
#include "../include/Tracer.h"

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

bool WatchPoint::set(void *addr, WatchPoint::E_Trigger trigger, E_Size size) {

    uint64_t dr7 = _tracer.commandPTrace(true, PTRACE_PEEKUSER, _spiedThread.getTid(), offsetof(struct user, u_debugreg[7]), NULL);
    if (dr7 == -1) {
        std::cerr <<"WatchPoint::set : PTRACE_PEEKUSER for dr7 failed : " << strerror(errno);
        return false;
    }

    const uint64_t offset = offsetof(struct user, u_debugreg[0]) + _idx * sizeof(user::u_debugreg[0]);
    if (_tracer.commandPTrace(true, PTRACE_POKEUSER, _spiedThread.getTid(), offset, addr) == -1) {
        std::cerr <<"WatchPoint::set : PTRACE_POKEUSER for dri failed : " << strerror(errno);
        return false;
    }
    _addr = addr;

    // Reset dr7 bits corresponding to current watchpoint
    dr7 &= ~((0b11 << (_idx * 2)) | (0b1111 << (_idx * 4 + 16)));

    // Set dr7 bits corresponding to parameters
    dr7 |= (0b11 << (_idx * 2));
    dr7 |= (trigger << (_idx * 4 + 16));
    dr7 |= (size << (_idx * 4 + 18));

    if (_tracer.commandPTrace(true, PTRACE_POKEUSER, _spiedThread.getTid(), offsetof(struct user, u_debugreg[7]), dr7) == -1) {
        std::cerr << "WatchPoint::set : PTRACE_POKEUSER for dr7 failed : " << strerror(errno);
        return false;
    }

    _addr = addr;
    _isSet = true;

    return true;
}

bool WatchPoint::unset() {
    uint64_t dr7 = _tracer.commandPTrace(true, PTRACE_PEEKUSER, _spiedThread.getTid(),
                                         offsetof(struct user, u_debugreg[7]), NULL);
    if (dr7 == -1) {
        std::cerr << "WatchPoint::unset : PTRACE_PEEKUSER for dr7 failed : " << strerror(errno);
        return false;
    }

    dr7 &= ~(0b11 << (_idx * 2));

    if (_tracer.commandPTrace(true, PTRACE_POKEUSER, _spiedThread.getTid(),
                              offsetof(struct user, u_debugreg[7]), dr7) == -1)
    {
        std::cerr << "WatchPoint::unset : PTRACE_POKEUSER for dr7 failed : " << strerror(errno);
        return false;
    }

    _isSet = false;
    return true;
}

void WatchPoint::setOnHit(std::function<void(WatchPoint &, SpiedThread &)>&& onHit) {
    std::lock_guard lk(_callbackMutex);

    _onHit = onHit;
}

void WatchPoint::hit() {
    std::lock_guard lk(_callbackMutex);

    _onHit(*this, _spiedThread);
}

void *WatchPoint::getAddr() {
    return _addr;
}

bool WatchPoint::isSet() const{
    return _isSet;
}

