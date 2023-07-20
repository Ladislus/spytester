#ifndef SPYTESTER_SPIEDTHREAD_H
#define SPYTESTER_SPIEDTHREAD_H


#include <condition_variable>
#include <csignal>
#include <future>
#include <mutex>
#include <sys/user.h>

#include "CallbackHandler.h"
#include "WatchPoint.h"

class Tracer;
class SpiedProgram;

class SpiedThread {
public:

    typedef enum {
        UNDETERMINED,
        STOPPED,
        CONTINUED,
        TERMINATED,
        EXITED
    } E_State;

    SpiedThread(Tracer &tracer, CallbackHandler &callbackHandler, pid_t tid);
    SpiedThread(SpiedThread&& spiedThread) = delete;
    SpiedThread(const SpiedThread& ) = delete;
    ~SpiedThread();

    pid_t getTid() const;

    void setState(E_State state);

    bool handleEvent(E_State state, int signal, int status, uint16_t ptraceEvent);

    void jump(void* addr);

    bool resume(int signum = 0);
    bool singleStep();
    bool stop();
    bool terminate();

    bool backtrace();
    bool detach();

    uint64_t getRip();

    inline bool operator==(pid_t tid) const{
        return tid == _tid;
    }

    WatchPoint* createWatchPoint();
    void deleteWatchPoint(WatchPoint* watchPoint);

private:
    typedef enum {
        OLD,
        SYNC,
        NEW
    } E_RegSync;

    void readRegisters();
    void writeRegisters();

    uint64_t getRbp();
    uint64_t getDr6();
    void setDr6(uint64_t dr6);

    const pid_t _tid;

    struct user_regs_struct _regs;
    std::future<std::pair<long, int>> _futureRegs;

    uint64_t _dr6;
    std::future<std::pair<long, int>> _futureDr6;

    E_RegSync _regSync;

    std::recursive_mutex _stateMutex;
    std::condition_variable_any _stateCV;

    E_State _state;

    bool _isSigTrapExpected;

    std::vector<std::pair<std::unique_ptr<WatchPoint>, bool>> _watchPoints;
    Tracer& _tracer;
    CallbackHandler& _callbackHandler;
};


#endif //SPYTESTER_SPIEDTHREAD_H
