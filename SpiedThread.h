#ifndef SPYTESTER_SPIEDTHREAD_H
#define SPYTESTER_SPIEDTHREAD_H


#include <csignal>
#include <mutex>
#include <condition_variable>
#include "TracingCommand.h"
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
    SpiedThread(SpiedThread&& spiedThread) = default;
    SpiedThread(const SpiedThread& ) = delete;
    ~SpiedThread();

    pid_t getTid() const;

    E_State getState();
    void setState(E_State state);

    void handleSigTrap();
    void handleSigStop();

    void resume(int signum = 0);
    void singleStep();
    void stop();
    void terminate();

    WatchPoint* createWatchPoint();
    void deleteWatchPoint(WatchPoint* watchPoint);

private:
    const pid_t _tid;
    std::mutex _stateMutex;
    std::condition_variable _stateCV;
    E_State _state;

    bool _isSigTrapExpected;

    std::vector<std::pair<WatchPoint, bool>> _watchPoints;
    Tracer& _tracer;

    SpiedProgram& _spiedProgram;
    using SpiedThreadCmd = TracingCommand<SpiedThread>;

    using ResumeCmd = TracingCommand<SpiedThread, int>;
};


#endif //SPYTESTER_SPIEDTHREAD_H
