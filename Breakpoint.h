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

    void privSet();
    void privUnset();

    TracingCommand<BreakPoint> _setCommand;
    TracingCommand<BreakPoint> _unsetCommand;

public:
    BreakPoint(Tracer& tracer, const std::string&& name, void* addr);
    ~BreakPoint() = default;

    void set();
    void unset();

    void restart(SpiedThread& spiedThread);

    void hit(SpiedThread& spiedThread);

};


#endif //SPYTESTER_BREAKPOINT_H
