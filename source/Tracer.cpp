#include <sys/ptrace.h>
#include <iostream>
#include <cstring>
#include <wait.h>
#include <unistd.h>
#include "../include/Tracer.h"
#include "../include/SpiedProgram.h"

#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)
#include <dlfcn.h>

#define tgkill(tgid, tid, sig) syscall(SYS_tgkill, tgid, tid, sig)

Tracer::Tracer()
: _state(NOT_STARTED)
{
    if(sem_init(&_cmdsSem, 0, 0) == -1){
        std::cerr << __FUNCTION__ << " : semaphore initialization failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("invalid sem init");
    }

    _stack = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (_stack == MAP_FAILED) {
        std::cerr << __FUNCTION__ << " : stack allocation failed : " << strerror(errno) << std::endl;
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
        std::cerr << __FUNCTION__ <<" : clone failed : "<< strerror(errno) <<std::endl;
        return;
    }

    // Wait for spied program process to be created
    int wstatus;
    if(waitpid(_traceePid, &wstatus, 0) == -1){
        std::cerr << "waitpid failed : " << strerror(errno) << std::endl;
    }
    if (WIFSTOPPED(wstatus)){
        if(ptrace(PTRACE_SETOPTIONS, _traceePid, nullptr, PTRACE_O_TRACECLONE) == -1){
            std::cerr << __FUNCTION__ <<" : PTRACE_O_TRACECLONE failed : "<< strerror(errno) <<std::endl;
        }
        if(ptrace(PTRACE_CONT, _traceePid, nullptr, nullptr) == -1){
            std::cerr << __FUNCTION__ <<" : PTRACE_CONT failed for starter" << std::endl;
        }
    }

    // Wait for pre start to create the main thread
    if(waitpid(_traceePid, &wstatus, 0) == -1){
        std::cerr << __FUNCTION__ << " : waitpid failed (create starter): " << strerror(errno) << std::endl;
    }
    if(ptrace(PTRACE_CONT, _traceePid, nullptr, nullptr) == -1){
        std::cerr << __FUNCTION__ <<" : PTRACE_CONT failed for starter" << std::endl;
    }
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
    std::cout << __FUNCTION__ << " : stop handling commands!" << std::endl;
}


int Tracer::preStart(void * spiedNamespace) {
    if(ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
        std::cerr<< __FUNCTION__ << " : PTRACE_TRACEME failed : " << strerror(errno) << std::endl;
        std::exit(1);
    }
    if( raise(SIGSTOP) != 0)
    {
        std::cerr<<__FUNCTION__<<" : ptrace failed : " << strerror(errno) << std::endl;
        std::exit(1);
    }
    DynamicNamespace::createMainThread(reinterpret_cast<DynamicNamespace *>(spiedNamespace));
    return 0;
}

int Tracer::tkill(pid_t tid, int sig) {
    _cmdsMutex.lock();
    _commands.emplace([this, tid, sig]{
        if(tgkill(_traceePid, tid, sig) == -1){
            std::cerr << "tgkill("<< tid <<", "<<sig<<") failed : " << strerror(errno) << std::endl;
        }
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


    std::cout<< __FUNCTION__ << std::endl;
}

std::future<std::pair<long, int>> Tracer::writeWord(void *addr, uint64_t val) {
    return commandPTrace(PTRACE_POKEDATA, _traceePid, addr, val);
}
