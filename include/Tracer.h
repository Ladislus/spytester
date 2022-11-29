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

#include <sys/ptrace.h>
#include <semaphore.h>
#include <pthread.h>
#include <cstring>

#include "SpiedThread.h"

class SpiedProgram;

extern "C" {
    extern void _asm_starter(void*, uint64_t, char*, char*);
};

class Tracer {
public :

    explicit Tracer(SpiedProgram &spiedProgram);
    Tracer(const Tracer&) = delete;
    Tracer(Tracer&&) = delete;
    ~Tracer();

    void start();

    pid_t getTraceePid() const;

    template<typename ... Args>
    long commandPTrace(bool sync, enum __ptrace_request request, Args ... args);

    int tkill(pid_t tid, int sig);


private:
    typedef enum {
        NOT_STARTED,
        STARTING,
        TRACING,
        STOPPED,
    } E_State;

    static int preStarter(void* param);
    static void* starter(void* param);
    static void * commandHandler(void* tracer);
    static void * eventHandler(void* tracer);
    static void * callbackHandler(void* tracer);

    SpiedProgram& _spiedProgram;

    pid_t _traceePid;
    pid_t _traceeTid;

    pthread_attr_t _attr;

    pthread_t _callbackHandler;
    pthread_t _evtHandler;
    pthread_t _cmdHandler;
    pthread_t _starter;

    volatile E_State _state;
    std::mutex _stateMutex;
    std::condition_variable _stateCV;

    sem_t _callbackSem;
    std::mutex _callbackMutex;

    sem_t _cmdsSem;
    std::mutex _cmdsMutex;

    std::queue<std::function<void()>> _commands;
    std::queue<std::function<void()>> _callbacks;

    void executeCallback(std::function<void()>&& callback);
    void setState(E_State state);
    void initTracer();
    void handleCommand();
    void handleEvent();
    void handleCallback();
};

template<typename ... Args>
long Tracer::commandPTrace(bool sync, enum __ptrace_request request, Args ... args)
{
    long res = 0L;
    if(sync) { // synchronous
        std::mutex resMutex;
        std::condition_variable resCV;
        std::unique_lock lk (resMutex);

        bool cmdExecuted= false;
        int* perrno = &errno;

        _cmdsMutex.lock();
        _commands.emplace(([&res, perrno, &cmdExecuted, &resMutex, &resCV, request, args...]{
            resMutex.lock();

            res = ptrace(request, args ...);
            *perrno = errno;

            resMutex.unlock();

            cmdExecuted = true;
            resCV.notify_all();
        }));
        _cmdsMutex.unlock();

        sem_post(&_cmdsSem);
        resCV.wait(lk, [&cmdExecuted]{return cmdExecuted;});

    } else { // asynchronous
        _cmdsMutex.lock();
        _commands.emplace([request, args ...] {
            if(ptrace(request, args ...) == -1) {
                std::cerr << "ptrace(request = " << request << ") failed : "
                          << strerror(errno) << std::endl;
            }
        });
        _cmdsMutex.unlock();
        sem_post(&_cmdsSem);
    }

    return res;
}

#endif //SPYTESTER_TRACER_H
