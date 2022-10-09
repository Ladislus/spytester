#include <sys/ptrace.h>
#include <iostream>
#include <cstring>
#include <wait.h>
#include <unistd.h>
#include <sys/mman.h>
#include "Tracer.h"
#include "SpiedProgram.h"

#include <sys/syscall.h>
#include <dlfcn.h>

#define gettid() syscall(SYS_gettid)

const size_t Tracer::stackSize =  1<<23;

int Tracer::starter(void *param) {
    auto progParam = (ProgParam *) param;

    if(ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
        std::cerr<< __FUNCTION__ << " : PTRACE_TRACEME failed : " << strerror(errno) << std::endl;
        std::exit(1);
    }

    if( raise(SIGTRAP) != 0)
    {
        std::cerr<<__FUNCTION__<<" : ptrace failed : " << strerror(errno) << std::endl;
        std::exit(1);
    }

    _asm_starter(progParam->entryPoint, progParam->argc, progParam->argv, progParam->envp);

    return 0;
}

int Tracer::commandHandler(void * tracer) {
    auto t = (Tracer*) tracer;

    if(t == nullptr)
    {
        std::cerr << __FUNCTION__ << " : unexpected pid " << getpid() << std::endl;
        return 1;
    }
    t->initTracer();
    t->handleCommand();

    return 0;
}

int Tracer::eventHandler(void* tracer) {
    auto t = (Tracer *) tracer;
    if (t == nullptr) {
        std::cerr << __FUNCTION__ << " : unexpected pid " << getpid() << std::endl;
        return 1;
    }

    t->handleEvent();

    return 0;
}

void Tracer::handleEvent() {
    std::cout << __FUNCTION__ << " : start handling events!" << std::endl;

    int wstatus;
    pid_t tid;
    // Wait until all child threads exit
    while((tid = waitpid(-1, &wstatus, WCONTINUED)) > 0)
    {
        SpiedThread& thread = _spiedProgram.getSpiedThread(tid);

        if(WIFSTOPPED(wstatus)){
            thread.setState(SpiedThread::STOPPED);
            int signum = WSTOPSIG(wstatus);
            std::cout << __FUNCTION__ << " : thread " << tid <<" stopped : SIG = " << signum<< std::endl;
            switch(signum)
            {
                case SIGTRAP:
                    thread.handleSigTrap();
                    break;
                case SIGSTOP:
                    break;
                default:
                    thread.resume(signum);
                    break;
            }
        }
        else if(WIFCONTINUED(wstatus))
        {
            thread.setState(SpiedThread::RUNNING);
            std::cout << __FUNCTION__  <<" : thread " << tid <<" resumed!" << std::endl;
        }
        else if(WIFEXITED(wstatus))
        {
            thread.setState(SpiedThread::TERMINATED);
            std::cout << __FUNCTION__ <<" : thread "<< tid <<" exits with code "<<WEXITSTATUS(wstatus)<<std::endl;
        }
        else if(WIFSIGNALED(wstatus))
        {
            thread.setState(SpiedThread::TERMINATED);
            std::cout << __FUNCTION__<<" : thread "<< tid <<" exits with signal "<<WTERMSIG(wstatus)<<std::endl;
        }
    }

    setState(STOPPING);
    std::cout << __FUNCTION__ << ": stop handling events!" << std::endl;

    // Wakeup command handler
    sem_post(&_cmdsSem);
}

void Tracer::handleCommand() {
    std::cout << __FUNCTION__ << " : start handling commands!" << std::endl;
    setState(STARTING);

    while(_state != STOPPING)
    {
        if(sem_wait(&_cmdsSem) != 0)
            break;

        //Get next command to execute
        _cmdsMutex.lock();
        if(!_commands.empty()) {
            auto cmd = std::move(_commands.front());
            _commands.pop();
            _cmdsMutex.unlock();

            cmd->execute();
        }
        else{
            _cmdsMutex.unlock();
        }
    }
    std::cout << __FUNCTION__ << " : stop handling commands!" << std::endl;
    setState(STOPPED);
}

Tracer::Tracer(SpiedProgram &spiedProgram)
: _spiedProgram(spiedProgram), _state(NOT_STARTED)
{
    if(sem_init(&_cmdsSem, 0, 0) == -1){
        std::cerr << __FUNCTION__ << " : semaphore initialization failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("invalid sem init");
    }

    _evtHandlerStack = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (_evtHandlerStack == MAP_FAILED) {
        std::cerr << __FUNCTION__ << " : stack allocation failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("Cannot allocate stack");
    }
    _cmdHandlerStack = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (_cmdHandlerStack == MAP_FAILED) {
        std::cerr << __FUNCTION__ << " : stack allocation failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("Cannot allocate stack");
    }

    std::unique_lock lk(_stateMutex);

    char* stackTop = (char *) _cmdHandlerStack + stackSize;
    if(clone(commandHandler, stackTop, CLONE_FS | CLONE_VM | CLONE_FILES, this) == -1){
        std::cerr << __FUNCTION__ << " : clone failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("Cannot launch _tracer thread");
    }

    // Wait for tracer threads to be started and ready to handle commands and events
    _stateCV.wait(lk, [this]{return _state == STARTING;});
}

void Tracer::command(std::unique_ptr<Command> cmd) {
    if(cmd != nullptr)
    {
        _cmdsMutex.lock();
        _commands.push(std::move(cmd));
        _cmdsMutex.unlock();
        sem_post(&_cmdsSem);
    }
}

Tracer::~Tracer() {
    std::unique_lock lk(_stateMutex);
    if(_state != STOPPED){
        // wait for tracing to be stopped
        _stateCV.wait(lk, [this](){return _state==STOPPED;});
    }

    sem_destroy(&_cmdsSem);
    // TODO munmap not safe : evtHandler/cmdHandler may not be completely terminated
    munmap(_evtHandlerStack, stackSize);
    munmap(_cmdHandlerStack, stackSize);
    std::cout<< __FUNCTION__ << std::endl;
}

pid_t Tracer::getTraceePid() const {
    return _traceePid;
}

bool Tracer::isTracerThread() const {
    return gettid() == _tracerTid;
}

void Tracer::setState(Tracer::E_State state) {
    _stateMutex.lock();
    _state = state;
    _stateMutex.unlock();
    _stateCV.notify_all();
}

void Tracer::start() {
    if(_state == STARTING) {
        setState(TRACING);
        SpiedThread& sp  = _spiedProgram.getSpiedThread(_traceePid);
        sp.resume();
    }
    else{
        std::cout << __FUNCTION__ << " : tracer has already been started " << std::endl;
    }
}

void Tracer::initTracer() {
    _traceePid = clone(starter, _spiedProgram.getStackTop(),
                       CLONE_FS | CLONE_VM | CLONE_FILES | SIGCHLD, _spiedProgram.getProgParam());
    if(_traceePid == -1){
        std::cerr << __FUNCTION__ <<" : clone failed : "<< strerror(errno) <<std::endl;
        return;
    }

    int wstatus;
    if(waitpid(_traceePid, &wstatus, 0) == -1){
        std::cerr << "waitpid failed : " << strerror(errno) << std::endl;
    }
    if (WIFSTOPPED(wstatus)){
        if(ptrace(PTRACE_SETOPTIONS, _traceePid, NULL, PTRACE_O_TRACECLONE) == -1){
            std::cerr << __FUNCTION__ <<" : PTRACE_O_TRACECLONE failed : "<< strerror(errno) <<std::endl;
        }
    }

    _tracerTid = gettid();

    if (clone(eventHandler, (char *) _evtHandlerStack + stackSize,
              CLONE_FS | CLONE_VM | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD, this) == -1){
        std::cerr << __FUNCTION__ <<" : failed to clone eventHandler : " << strerror(errno) << std::endl;
    }
}
