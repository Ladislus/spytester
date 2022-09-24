#ifndef SPYTESTER_SPIEDTHREAD_H
#define SPYTESTER_SPIEDTHREAD_H


#include <csignal>
#include <mutex>
#include "TracingCommand.h"

class Tracer;

class SpiedThread {
    const pid_t _tid;

    std::mutex _isRunningMutex;
    bool _isRunning;

    Tracer& _tracer;

    using SpiedThreadCmd = TracingCommand<SpiedThread>;

public:
    explicit SpiedThread(Tracer& tracer, pid_t tid);

    ~SpiedThread();

    pid_t getTid() const;
    bool isRunning() ;

    void setRunning(bool isRunning);
    void resume();

    void stop();
};


#endif //SPYTESTER_SPIEDTHREAD_H
