#ifndef SPYTESTER_TRACER_H
#define SPYTESTER_TRACER_H

#include <csignal>
#include <cstring>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <semaphore.h>
#include <set>
#include <sys/ptrace.h>
#include <thread>

#include "DynamicNamespace.h"
#include "SpiedThread.h"

class SpiedProgram;
class DynamicLinker;

class Tracer {
public :

    Tracer();
    Tracer(const Tracer&) = delete;
    Tracer(Tracer&&) = delete;
    ~Tracer();

    pid_t startTracing(DynamicNamespace& spiedNamespace);

    template<typename ... Args>
    std::future<std::pair<long, int>>
    commandPTrace(enum __ptrace_request request, Args ... args);

    std::future<std::pair<long, int>>
    writeWord(void* addr, uint64_t val);

    int tkill(pid_t tid, int sig);

private:
    typedef enum {
        NOT_STARTED,
        STARTING,
        TRACING,
        STOPPED,
    } E_State;

    static int preStart(void* param);

    void* _stack;

    pid_t _traceePid;

    std::thread _tracer;

    volatile E_State _state;
    std::mutex _stateMutex;
    std::condition_variable _stateCV;

    sem_t _cmdsSem;
    std::mutex _cmdsMutex;

    std::queue<std::function<void()>, std::list<std::function<void()>>> _commands;

    void setState(E_State state);
    void trace(DynamicNamespace &spiedNamespace, std::promise<pid_t> promise);
    void createTracee(DynamicNamespace &spiedNamespace);
};

template<typename ... Args>
std::future<std::pair<long, int>>
Tracer::commandPTrace(enum __ptrace_request request, Args ... args)
{
    auto promise = std::make_shared< std::promise<std::pair<long, int>> >();
    auto future = promise->get_future();

    _cmdsMutex.lock();
    _commands.emplace([promise, request, args ...]{
        long res = ptrace(request, args ...);
        promise->set_value(std::make_pair(res, errno));
    });

    _cmdsMutex.unlock();
    sem_post(&_cmdsSem);

    return future;
}

#endif //SPYTESTER_TRACER_H
