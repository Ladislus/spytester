#ifndef SPYTESTER_BREAKPOINT_H
#define SPYTESTER_BREAKPOINT_H


#include <queue>
#include <string>
#include <vector>
#include <mutex>
#include <sstream>

#include "Tracer.h"
#include "CallbackHandler.h"
#include "SpiedThread.h"

class BreakPoint {
private:

    using BreakpointCallback = std::function<void(BreakPoint&, SpiedThread&)>;
    using Mutex = std::recursive_mutex;
    using LockGuard = std::lock_guard<Mutex>;

    const static uint8_t INT3 = 0xCC;

    Mutex _breakPointMutex;
    const std::string _name;
    uint64_t * const _addr;
    uint64_t _backup;
    bool _isSet;
    Tracer& _tracer;
    CallbackHandler& _callbackHandler;

    // callback function
    BreakpointCallback _onHit;

    // default callback function
    static void defaultOnHit(BreakPoint& breakPoint, SpiedThread& spiedThread);

public:

    BreakPoint(Tracer &tracer, CallbackHandler &callbackHandler, const std::string &&name, void* addr);
    ~BreakPoint() = default;

    void* getAddr() const;

    bool set();
    bool unset();

    void setOnHitCallback(BreakpointCallback&& callback);
    void hit(SpiedThread& spiedThread);

    bool resumeAndUnset(SpiedThread &spiedThread);
    bool resumeAndSet(SpiedThread &spiedThread);

    inline bool operator==(void* addr) const { return addr == this->_addr; }
};


#endif //SPYTESTER_BREAKPOINT_H
