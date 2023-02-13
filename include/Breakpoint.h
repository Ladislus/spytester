//
// Created by baptiste on 14/09/22.
//

#ifndef SPYTESTER_BREAKPOINT_H
#define SPYTESTER_BREAKPOINT_H


#include <string>
#include <vector>
#include <queue>
#include "Tracer.h"

class BreakPoint {
public:
    BreakPoint(Tracer &tracer, CallbackHandler &callbackHandler, const std::string &&name, void *addr);

    ~BreakPoint() = default;

    void* getAddr() const;

    bool set();
    bool unset();

    void setOnHitCallback(std::function<void (BreakPoint&, SpiedThread&)>&& callback);
    void hit(SpiedThread& spiedThread);

    bool resumeAndUnset(SpiedThread &spiedThread);
    bool resumeAndSet(SpiedThread &spiedThread);

    inline bool operator==(void* addr) const{
        return addr == _addr;
    }

private:
    const static uint8_t INT3 = 0xCC;

    std::recursive_mutex _breakPointMutex;

    const std::string _name;
    uint64_t * const _addr;
    uint64_t _backup;
    bool _isSet;
    Tracer& _tracer;
    CallbackHandler& _callbackHandler;

    // callback function
    std::function<void (BreakPoint&, SpiedThread&)> _onHit;

    // default callback function
    static void defaultOnHit(BreakPoint& breakPoint, SpiedThread& spiedThread);


};


#endif //SPYTESTER_BREAKPOINT_H
