#ifndef SPYTESTER_SPIEDTHREAD_H
#define SPYTESTER_SPIEDTHREAD_H


#include <csignal>
#include <mutex>
#include <condition_variable>
#include <sys/user.h>
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

    uint64_t getRip();
    uint64_t getRbp();
    void setRip(uint64_t rip);

    bool resume(int signum = 0);
    bool singleStep();
    bool stop();
    bool terminate();

    bool backtrace();
    bool detach();

    WatchPoint* createWatchPoint();
    void deleteWatchPoint(WatchPoint* watchPoint);

private:
    void setRegisters();
    void getRegisters();

    const pid_t _tid;

    struct user_regs_struct _regs;
    bool _isRegsDirty;
    bool _isRegsModified;

    std::recursive_mutex _stateMutex;
    std::condition_variable_any _stateCV;

    E_State _state;
    int _code;

    bool _isSigTrapExpected;

    std::vector<std::pair<std::unique_ptr<WatchPoint>, bool>> _watchPoints;
    Tracer& _tracer;

    SpiedProgram& _spiedProgram;
};


#endif //SPYTESTER_SPIEDTHREAD_H
