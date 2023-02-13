#include <iostream>
#include <sys/procfs.h>

#include "../include/Breakpoint.h"

BreakPoint::BreakPoint(Tracer &tracer, CallbackHandler &callbackHandler, const std::string &&name, void *addr) :
_addr((uint64_t *)addr), _name(name), _isSet(false), _tracer(tracer),
_callbackHandler(callbackHandler), _onHit(BreakPoint::defaultOnHit){}

void *BreakPoint::getAddr() const{
    return _addr;
}

bool BreakPoint::set(){
    if (!_isSet) {
        _backup = *_addr;
        uint64_t newWord = (_backup & (~0xFF)) | INT3;

        _tracer.writeWord(_addr, newWord);
        std::cout << "BreakPoint::set : breakpoint (" << _name << ") set at " << _addr << std::endl;
        _isSet = true;
    }
    return _isSet;
}


bool BreakPoint::unset(){
    if (_isSet) {
        _tracer.writeWord(_addr, _backup);
        std::cout << "BreakPoint::unset : breakpoint (" << _name << ") unset" << std::endl;
        _isSet = false;
    }
    return !_isSet;
}

bool BreakPoint::resumeAndSet(SpiedThread &spiedThread)
{
    struct timeval start, stop;
    gettimeofday(&start, nullptr);

    spiedThread.jump((void*)(spiedThread.getRip()-1));
    bool res = unset()
            && spiedThread.singleStep()
            && set()
            && spiedThread.resume();

    gettimeofday(&stop, nullptr);

    std::cerr << __FUNCTION__ << " executed in : "
            << (stop.tv_sec - start.tv_sec)*1000000 + (stop.tv_usec - start.tv_usec) << std::endl;

    return res;
}

bool BreakPoint::resumeAndUnset(SpiedThread &spiedThread) {
    spiedThread.jump((void*)(spiedThread.getRip()-1));
    return unset() && spiedThread.resume();
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
    _breakPointMutex.lock();
    _callbackHandler.executeCallback([this, &spiedThread]{_onHit(*this, spiedThread);});
    _breakPointMutex.unlock();
}

