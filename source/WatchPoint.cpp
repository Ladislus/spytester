#include <cstring>
#include <sys/ptrace.h>
#include <sys/user.h>

#include "Tracer.h"
#include "WatchPoint.h"
#include "Logger.h"

void defaultOnHit(WatchPoint& watchPoint, SpiedThread& spiedThread){
    info_log("Thread " << spiedThread.getTid() << " hits watchpoint at " << watchPoint.getAddr());
}

WatchPoint::WatchPoint(Tracer &tracer, CallbackHandler &callbackHandler, SpiedThread &spiedThread, uint32_t idx):
    _tracer(tracer),
    _callbackHandler(callbackHandler),
    _spiedThread(spiedThread),
    _addr(nullptr),
    _idx(idx),
    _isSet(false),
    _onHit(defaultOnHit) {}

bool WatchPoint::set(void *addr, WatchPoint::E_Trigger trigger, E_Size size) {
    // Read DR7 register
    auto futureRes = this->_tracer.commandPTrace(PTRACE_PEEKUSER, this->_spiedThread.getTid(), offsetof(struct user, u_debugreg[7]), NULL);

    const uint64_t offset = offsetof(struct user, u_debugreg[0]) + _idx * sizeof(user::u_debugreg[0]);
    this->_tracer.commandPTrace(PTRACE_POKEUSER, _spiedThread.getTid(), offset, addr);
    this->_addr = addr;

    // Reset dr7 bits corresponding to current watchpoint
    auto res = futureRes.get();
    if(res.first == -1){
        error_log("PTRACE_PEEKUSER for dr7 failed (" << strerror(res.second) << ")");
        return false;
    }

    uint64_t dr7 = res.first;
    dr7 &= ~((0b11 << (this->_idx * 2)) | (0b1111 << (this->_idx * 4 + 16)));

    // Set dr7 bits corresponding to parameters
    dr7 |= (0b11    << (this->_idx * 2     ));
    dr7 |= (trigger << (this->_idx * 4 + 16));
    dr7 |= (size    << (this->_idx * 4 + 18));

    this->_tracer.commandPTrace(
        PTRACE_POKEUSER,
        this->_spiedThread.getTid(),
        offsetof(struct user, u_debugreg[7]),
        dr7
    );

    this->_addr = addr;
    this->_isSet = true;

    return true;
}

bool WatchPoint::unset() {
    auto resFuture = this->_tracer.commandPTrace(
        PTRACE_PEEKUSER,
        this->_spiedThread.getTid(),
        offsetof(struct user, u_debugreg[7]),
        NULL
    );
    auto res = resFuture.get();

    if (res.first == -1) {
        error_log("PTRACE_PEEKUSER for dr7 failed : " << strerror(res.second));
        return false;
    }

    uint64_t dr7 = res.first;
    dr7 &= ~(0b11 << (this->_idx * 2));

    this->_tracer.commandPTrace(
        PTRACE_POKEUSER,
        this->_spiedThread.getTid(),
        offsetof(struct user, u_debugreg[7]),
        dr7
    );

    this->_isSet = false;
    return true;
}

void WatchPoint::setOnHit(std::function<void(WatchPoint &, SpiedThread &)>&& onHit) {
    std::lock_guard lk(this->_callbackMutex);

    this->_onHit = onHit;
}

void WatchPoint::hit() {
    std::lock_guard lk(_callbackMutex);
    this->_callbackHandler.executeCallback([this] { this->_onHit(*this, this->_spiedThread); });
}

void *WatchPoint::getAddr() { return this->_addr; }

bool WatchPoint::isSet() const { return this->_isSet; }

