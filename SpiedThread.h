//
// Created by baptiste on 13/09/22.
//

#ifndef SPYTESTER_SPIEDTHREAD_H
#define SPYTESTER_SPIEDTHREAD_H


#include <csignal>
#include <mutex>
#include "TracingCommand.h"

class Tracer;

class SpiedThread {
    const pid_t _pid;

    std::mutex _isRunningMutex;
    bool _isRunning;

    Tracer& _tracer;

    TracingCommand<SpiedThread> _resumeCommand;
    TracingCommand<SpiedThread> _stopCommand;

public:
    explicit SpiedThread(Tracer& tracer, pid_t pid);
    ~SpiedThread();

    pid_t getPid() const;

    bool isRunning() ;
    void setRunning(bool isRunning);

    void resume();
    void stop();
};


#endif //SPYTESTER_SPIEDTHREAD_H
