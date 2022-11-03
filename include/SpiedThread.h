#ifndef SPYTESTER_SPIEDTHREAD_H
#define SPYTESTER_SPIEDTHREAD_H


#include <csignal>
#include <mutex>
#include <condition_variable>
#include "WatchPoint.h"

class Tracer;
class SpiedProgram;

class SpiedThread {
public:

    typedef enum {
        STOPPED,
        RUNNING,
        TERMINATED
    } E_State;

    SpiedThread(SpiedProgram& spiedProgram, Tracer& tracer, pid_t tid);
    SpiedThread(SpiedThread&& spiedThread) = delete;
    SpiedThread(const SpiedThread& ) = delete;
    ~SpiedThread();

    pid_t getTid() const;

    void setState(E_State state, int code = 0);

    bool handleSigTrap(int wstatus);
    void handleSigStop();

    bool resume(int signum = 0);
    bool singleStep();
    bool stop();
    bool terminate();

    bool backtrace();
    bool detach();

    WatchPoint* createWatchPoint();
    void deleteWatchPoint(WatchPoint* watchPoint);

private:
    const pid_t _tid;

    std::mutex _stateMutex;
    std::unique_lock<std::mutex> _stateLock;
    std::condition_variable _stateCV;

    E_State _state;
    int _code;

    bool _isSigTrapExpected;

    std::vector<std::pair<WatchPoint, bool>> _watchPoints;
    Tracer& _tracer;

    SpiedProgram& _spiedProgram;
};


#endif //SPYTESTER_SPIEDTHREAD_H
