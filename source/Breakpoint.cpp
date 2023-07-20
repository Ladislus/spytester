#include <iostream>
#include <sys/procfs.h>

#include "Breakpoint.h"

BreakPoint::BreakPoint(Tracer &tracer, CallbackHandler &callbackHandler, const std::string &&name, void *addr) :
    _addr((uint64_t *)addr),
    _name(name),
    _isSet(false),
    _tracer(tracer),
    _callbackHandler(callbackHandler),
    _onHit(BreakPoint::defaultOnHit) {}

void* BreakPoint::getAddr() const { return this->_addr; }

bool BreakPoint::set() {
    if (!this->_isSet) {
        this->_backup = *this->_addr;

        uint64_t newWord = (this->_backup & (~0xFF)) | INT3;
        this->_tracer.writeWord(this->_addr, newWord);

        info_log("Breakpoint (" << _name << ") set at " << _addr);

        this->_isSet = true;
    }

    return this->_isSet;
}


bool BreakPoint::unset() {
    if (this->_isSet) {
        this->_tracer.writeWord(this->_addr, this->_backup);
        info_log("BreakPoint (" << _name << ") unset");
        this->_isSet = false;
    }
    return !this->_isSet;
}

bool BreakPoint::resumeAndSet(SpiedThread &spiedThread)
{
    struct timeval start, stop;
    gettimeofday(&start, nullptr);

    spiedThread.jump((void*)(spiedThread.getRip()-1));
    bool res =
            unset()
            && spiedThread.singleStep()
            && set()
            && spiedThread.resume();

    gettimeofday(&stop, nullptr);

    info_log("Executed in " << (stop.tv_sec - start.tv_sec) * 1'000'000 + (stop.tv_usec - start.tv_usec) << "ms");

    return res;
}

bool BreakPoint::resumeAndUnset(SpiedThread &spiedThread) {
    spiedThread.jump((void*)(spiedThread.getRip()-1));
    return unset() && spiedThread.resume();
}

void BreakPoint::setOnHitCallback(BreakpointCallback&& callback) {
    const LockGuard lk(this->_breakPointMutex);
    this->_onHit = callback;
}

void BreakPoint::defaultOnHit(BreakPoint& breakPoint, SpiedThread& spiedThread) {
    info_log("Thread " << spiedThread.getTid() << " hit breakpoint " << breakPoint._name + " at 0x" << std::hex << breakPoint._addr);
}

void BreakPoint::hit(SpiedThread &spiedThread) {
    defaultOnHit(*this, spiedThread);
    _breakPointMutex.lock();
    _callbackHandler.executeCallback([this, &spiedThread]{_onHit(*this, spiedThread);});
    _breakPointMutex.unlock();
}

