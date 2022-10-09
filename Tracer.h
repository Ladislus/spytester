//
// Created by baptiste on 13/09/22.
//

#ifndef SPYTESTER_TRACER_H
#define SPYTESTER_TRACER_H

#include <set>
#include <csignal>
#include <map>
#include <mutex>
#include <memory>

#include <semaphore.h>

#include "SpiedThread.h"
#include "TracingCommand.h"

class SpiedProgram;

extern "C" {
    extern void _asm_starter(void*, uint64_t, char*, char*);
};

class Tracer {
    typedef enum {
        NOT_STARTED,
        STARTING,
        TRACING,
        STOPPING,
        STOPPED,
    } E_State;

    static const size_t stackSize;

    static int starter(void* param);
    static int commandHandler(void* tracer);
    static int eventHandler(void* tracer);

    SpiedProgram& _spiedProgram;
    pid_t _tracerTid;
    pid_t _traceePid;
    void* _evtHandlerStack;
    void* _cmdHandlerStack;

    volatile E_State _state;
    std::mutex _stateMutex;
    std::condition_variable _stateCV;

    sem_t _cmdsSem;
    std::mutex _cmdsMutex;
    std::queue<std::unique_ptr<Command>> _commands;

    void setState(E_State state);

    void initTracer();
    void handleCommand();
    void handleEvent();

    using TracerCmd = TracingCommand<Tracer>;
public :
    explicit Tracer(SpiedProgram& spiedProgram);

    ~Tracer();

    void start();

    void command(std::unique_ptr<Command> cmd);

    pid_t getTraceePid() const;

    bool isTracerThread() const;
};



#endif //SPYTESTER_TRACER_H
