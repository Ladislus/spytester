//
// Created by baptiste on 14/09/22.
//

#ifndef SPYTESTER_BREAKPOINT_H
#define SPYTESTER_BREAKPOINT_H


#include <string>
#include <vector>
#include <queue>
#include "TracingCommand.h"
#include "Tracer.h"

class BreakPoint {
    const static uint8_t INT3 = 0xCC;

    const std::string _name;
    uint64_t * const _addr;
    uint64_t _backup;
    bool _isSet;
    Tracer& _tracer;

    void prepareToResume(SpiedThread& spiedThread);

    // callback function
    void(*_onHit)(BreakPoint&, SpiedThread&);

    // default callback function
    static void defaultOnHit(BreakPoint& breakPoint, SpiedThread& spiedThread);
    using BreakPointCmd = TracingCommand<BreakPoint>;

    using ResumeCmd = TracingCommand<BreakPoint, SpiedThread&>;
public:
    BreakPoint(Tracer& tracer, const std::string&& name, void* addr);

    ~BreakPoint() = default;

    void* getAddr() const;

    void set();
    void unset();

    void setOnHitCallback(void (*onHit)(BreakPoint&, SpiedThread&));
    void hit(SpiedThread& spiedThread);

    void resumeAndUnset(SpiedThread &spiedThread);
    void resumeAndSet(SpiedThread &spiedThread);
};


#endif //SPYTESTER_BREAKPOINT_H
