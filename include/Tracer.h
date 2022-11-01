#ifndef SPYTESTER_TRACER_H
#define SPYTESTER_TRACER_H

#include <set>
#include <csignal>
#include <map>
#include <mutex>
#include <memory>
#include <iostream>
#include <functional>
#include <queue>

#include <semaphore.h>
#include <pthread.h>

#include "SpiedThread.h"

class SpiedProgram;

extern "C" {
    extern void _asm_starter(void*, uint64_t, char*, char*);
};

typedef enum {
    UNDEFINED,
    SUCCESS,
    FAILURE
} E_CmdRes;

class Tracer {
public :

    using unique_cmd = std::unique_ptr<std::function<void()>>;
    static constexpr auto make_unique_cmd
        = std::make_unique<std::function<void()>, std::function<void()>>;

    explicit Tracer(SpiedProgram& spiedProgram);
    ~Tracer();

    void start();
    bool command(std::function<bool()> &&cmd, bool sync = true);

    pid_t getTraceePid() const;
    bool isTracerThread() const;

private:
    typedef enum {
        NOT_STARTED,
        STARTING,
        TRACING,
        STOPPING,
        STOPPED,
    } E_State;

    static int preStarter(void* param);
    static void* starter(void* param);
    static void * commandHandler(void* tracer);
    static void * eventHandler(void* tracer);

    SpiedProgram& _spiedProgram;

    pid_t _tracerTid;
    pid_t _starterTid;
    pid_t _traceeTid;

    pthread_attr_t _attr;

    pthread_t _evtHandler;
    pthread_t _cmdHandler;
    pthread_t _starter;

    volatile E_State _state;
    std::mutex _stateMutex;
    std::condition_variable _stateCV;

    sem_t _cmdsSem;
    std::mutex _cmdsMutex;

    std::queue<std::function<bool()>> _commands;

    void setState(E_State state);
    void initTracer();
    void handleCommand();
    void handleEvent();
};



#endif //SPYTESTER_TRACER_H
