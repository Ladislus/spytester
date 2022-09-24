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
    static const size_t stackSize;

    static int starter(void* param);
    static int tracerMain(void* tracer);
    static int eventHandler(void* tracer);

    SpiedProgram& _spiedProgram;
    pid_t _tracerPid;
    pid_t _tracerTid;
    pid_t _mainPid;
    void* _cmdrStack;
    void* _waitrStack;
    std::map<pid_t, SpiedThread> _spiedThreads;

    sem_t _cmdsSem;
    std::mutex _cmdsMutex;
    std::queue<std::unique_ptr<Command>> _commands;

    using HandleSigTrapCmd = TracingCommand<Tracer, SpiedThread&>;

public :
    explicit Tracer(SpiedProgram& spiedProgram);

    ~Tracer();
    void command(std::unique_ptr<Command> cmd);

    void stop();

    SpiedThread &getMainThread();

    pid_t getMainTid() const;

    bool isTracerThread() const;

    void handleCommand();

    void handleEvent();
};



#endif //SPYTESTER_TRACER_H
