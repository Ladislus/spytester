//
// Created by baptiste on 13/09/22.
//

#ifndef SPYTESTER_TRACER_H
#define SPYTESTER_TRACER_H

#include <set>
#include <csignal>
#include <map>
#include <mutex>
#include "SpiedThread.h"
#include "TracingCommand.h"


class SpiedProgram;

extern "C" {
    extern void _asm_starter(void*, uint64_t, char*, char*);
};

class Tracer {
    static const size_t stackSize;
    static std::map<pid_t, Tracer*> tracers;

    static int starter(void* param);
    static int tracerMain(void* tracer);
    static void sigHandler(int signum);


    SpiedProgram& _spiedProgram;
    pid_t _tracerPid;
    pid_t _mainPid;
    void* _stack;
    std::map<pid_t, SpiedThread> _spiedThreads;

    std::mutex _cmdsMutex;
    std::queue<Command*> _commands;

public :
    explicit Tracer(SpiedProgram& spiedProgram);
    ~Tracer();

    void command(Command* cmd);
    void stop();

    SpiedThread &getMainThread();

    pid_t getMainPid() const;

    void handleSigTrap(SpiedThread &spiedThread);

    pid_t getPid() const;
};



#endif //SPYTESTER_TRACER_H
