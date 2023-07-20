#ifndef SPYTESTER_WATCHPOINT_H
#define SPYTESTER_WATCHPOINT_H


#include <cstdint>
#include <functional>
#include <mutex>

#include "CallbackHandler.h"

class SpiedThread;
class Tracer;

class WatchPoint {
public:
    static const uint32_t maxNb = 4;

    typedef enum {
        EXECUTION = 0,
        WRITE = 1,
        READ_WRITE = 3
    } E_Trigger;

    typedef enum
    {
        _1BYTES = 0,
        _2BYTES = 1,
        _4BYTES = 3,
        _8BYTES = 2,
    } E_Size;

    WatchPoint(Tracer &tracer, CallbackHandler &callbackHandler, SpiedThread &spiedThread, uint32_t idx);
    ~WatchPoint() = default;

    void* getAddr();
    bool isSet() const;

    bool set(void *addr, WatchPoint::E_Trigger trigger, E_Size size);
    bool unset();

    void setOnHit(std::function<void(WatchPoint &, SpiedThread &)>&& onHit);
    void hit();

private:
    Tracer& _tracer;
    CallbackHandler& _callbackHandler;
    SpiedThread& _spiedThread;
    bool _isSet;
    const uint32_t _idx;
    void* _addr;
    std::mutex _callbackMutex;
    std::function<void(WatchPoint &, SpiedThread &)> _onHit;


};


#endif //SPYTESTER_WATCHPOINT_H
