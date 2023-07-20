#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <wait.h>

#include "SpiedProgram.h"
#include "Tracer.h"
#include "Logger.h"

#define gettid() syscall(SYS_gettid)

#define tgkill(tgid, tid, sig) syscall(SYS_tgkill, tgid, tid, sig)

Tracer::Tracer()
: _state(NOT_STARTED)
{
    if(sem_init(&_cmdsSem, 0, 0) == -1){
        error_log("Semaphore initialization failed : " << strerror(errno));
        throw std::invalid_argument("invalid sem init");
    }

    _stack = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (_stack == MAP_FAILED) {
        error_log("Stack allocation failed : " << strerror(errno));
        throw std::invalid_argument("Cannot allocate stack");
    }
}

pid_t Tracer::startTracing(DynamicNamespace& spiedNamespace) {
    std::promise<pid_t> promise;
    auto future = promise.get_future();

    _tracer = std::thread(&Tracer::trace, this, std::ref(spiedNamespace), std::move(promise));
    return future.get();
}

void Tracer::createTracee(DynamicNamespace &spiedNamespace){

    int cloneFlags = CLONE_FS | CLONE_FILES | SIGCHLD | CLONE_VM;
    void* stackTop = (void*)((uint64_t)_stack + stackSize);
    _traceePid = clone(preStart, stackTop, cloneFlags, &spiedNamespace);

    if(_traceePid == -1){
        error_log("Clone failed : " << strerror(errno));
        return;
    }

    // Wait for spied program process to be created
    int wstatus;
    if(waitpid(_traceePid, &wstatus, 0) == -1){
        error_log("Waitpid failed " << strerror(errno));
    }
    if (WIFSTOPPED(wstatus)) {
        if(ptrace(PTRACE_SETOPTIONS, _traceePid, nullptr, PTRACE_O_TRACECLONE) == -1)
            error_log("PTRACE_O_TRACECLONE failed : " << strerror(errno));

        if(ptrace(PTRACE_CONT, _traceePid, nullptr, nullptr) == -1)
            error_log("PTRACE_CONT failed for starter");
    }

    // Wait for pre start to create the main thread
    if(waitpid(_traceePid, &wstatus, 0) == -1)
        error_log("Waitpid failed (create starter): " << strerror(errno));

    if(ptrace(PTRACE_CONT, _traceePid, nullptr, nullptr) == -1)
        error_log("PTRACE_CONT failed for starter");
}

void Tracer::trace(DynamicNamespace &spiedNamespace, std::promise<pid_t> promise){

    createTracee(spiedNamespace);
    promise.set_value(_traceePid);

    setState(STARTING);

    while(_state != STOPPED)
    {
        sem_wait(&_cmdsSem);

        //Get next command to execute
        _cmdsMutex.lock();
        while(!_commands.empty()) {
            auto& command = _commands.front();
            _cmdsMutex.unlock();

            command();

            _cmdsMutex.lock();
            _commands.pop();
        }
        _cmdsMutex.unlock();
    }

    info_log("Stop handling commands!");
}


int Tracer::preStart(void * spiedNamespace) {
    if(ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1)
        fatal_log("PTRACE_TRACEME failed " << strerror(errno));

    if(raise(SIGSTOP) != 0)
        fatal_log("Ptrace failed " << strerror(errno));

    DynamicNamespace::createMainThread(reinterpret_cast<DynamicNamespace *>(spiedNamespace));
    return 0;
}

int Tracer::tkill(pid_t tid, int sig) {
    _cmdsMutex.lock();
    _commands.emplace([this, tid, sig] {
        if(tgkill(_traceePid, tid, sig) == -1)
            error_log("tgkill(" << tid << ", " << sig << ") failed (" << strerror(errno) << ")");

        return true;
    });
    _cmdsMutex.unlock();

    sem_post(&_cmdsSem);

    return 0;
}

void Tracer::setState(Tracer::E_State state) {
    _stateMutex.lock();
    _state = state;
    _stateMutex.unlock();
    _stateCV.notify_all();
}

Tracer::~Tracer() {

    munmap(_stack, stackSize);

    setState(STOPPED);
    sem_post(&_cmdsSem);

    _tracer.join();
    sem_destroy(&_cmdsSem);
}

std::future<std::pair<long, int>> Tracer::writeWord(void *addr, uint64_t val) {
    return commandPTrace(PTRACE_POKEDATA, _traceePid, addr, val);
}
