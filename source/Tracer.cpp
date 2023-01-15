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

Tracer::Tracer(SpiedProgram &spiedProgram)
: _spiedProgram(spiedProgram), _state(NOT_STARTED)
{
    if(sem_init(&_cmdsSem, 0, 0) == -1){
        std::cerr << __FUNCTION__ << " : semaphore initialization failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("invalid sem init");
    }

    if(sem_init(&_callbackSem, 0, 0) == -1){
        std::cerr << __FUNCTION__ << " : semaphore initialization failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("invalid sem init");
    }

    std::unique_lock lk(_stateMutex);

    pthread_attr_init(&_attr);

    int ret = pthread_create(&_cmdHandler, &_attr, commandHandler, this);
    if(ret !=0)
    {
        std::cerr << __FUNCTION__ << " : pthread_create failed : " << strerror(ret) << std::endl;
        throw std::invalid_argument("pthread_create failed");
    }

    _stateCV.wait(lk, [this]{return _state == STARTING;});
}

void * Tracer::commandHandler(void * tracer) {
    auto t = (Tracer*) tracer;

    if(t == nullptr)
    {
        std::cerr << __FUNCTION__ << " : unexpected pid " << getpid() << std::endl;
        return nullptr;
    }
    t->initTracer();
    t->handleCommand();

    return nullptr;
}

void Tracer::initTracer() {
    // Create spied program process
    int cloneFlags = CLONE_FS | CLONE_FILES | SIGCHLD | CLONE_VM;
    _traceePid = clone(preStarter, _spiedProgram.getStackTop(), cloneFlags, this);

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
        if(ptrace(PTRACE_SETOPTIONS, _traceePid, nullptr, PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK) == -1){
            std::cerr << __FUNCTION__ <<" : PTRACE_O_TRACECLONE failed : "<< strerror(errno) <<std::endl;
        }
        if(ptrace(PTRACE_CONT, _traceePid, nullptr, nullptr) == -1){
            std::cerr << __FUNCTION__ <<" : PTRACE_CONT failed for starter" << std::endl;
        }
    }

    // Wait for pre starter to create starter thread
    if(waitpid(_traceePid, &wstatus, 0) == -1){
        std::cerr << __FUNCTION__ << " : waitpid failed (create starter): " << strerror(errno) << std::endl;
    }

    _traceeTid = wait(&wstatus);
    if((_traceeTid == -1) || !WIFSTOPPED(wstatus)){
        std::cerr << "wait failed : " << strerror(errno) << std::endl;
    }

    // Start event handler
    int ret = pthread_create(&_evtHandler, &_attr, eventHandler, this);
    if(ret != 0){
        std::cerr << __FUNCTION__ <<" : pthread_create failed : "<< strerror(ret) << std::endl;
    }
}

int Tracer::preStarter(void * tracer) {

    auto t = (Tracer*) tracer;

    if(ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
        std::cerr<< __FUNCTION__ << " : PTRACE_TRACEME failed : " << strerror(errno) << std::endl;
        std::exit(1);
    }

    if( raise(SIGTRAP) != 0)
    {
        std::cerr<<__FUNCTION__<<" : ptrace failed : " << strerror(errno) << std::endl;
        std::exit(1);
    }
    pthread_create(&(t->_starter), &(t->_attr), starter, t->_spiedProgram.getProgParam());
    return 0;
}

void *Tracer::starter(void *param) {
    auto progParam = (struct ProgParam *) param;

    //_asm_starter(progParam->entryPoint, progParam->argc, progParam->argv, progParam->envp);
    uint64_t* sp;
    __asm__ volatile (" movq %%rsp, %0" : "=r"(sp)::);

    // Push environment pointers onto stack
    char** env = progParam->envp;
    for(; *env != nullptr; env++); // set env point to the last environment variable

    sp--;
    *sp = 0LU;

    for(; env >= progParam->envp; env--){
        sp--;
        *sp = (uint64_t)*env;
    }

    // Push argument pointers onto the stack
    sp--;
    *sp = 0LU;

    for(int i = progParam->argc - 1; i>=0; i--){
        sp--;
        *sp = (uint64_t)progParam->argv[i];
    }

    // Push argc onto the stack
    sp--;
    *sp = progParam->argc;

    //std::cout << "HOLA" << std::endl;

    __asm__ volatile(
          "xor %%rbp, %%rbp \n"
          "xor %%rdx, %%rdx \n"
          "movq %0, %%rsp \n"
          "jmp *%1 \n"
          ::"r"(sp), "r"(progParam->entryPoint): "%rdx", "%rbp");

    return nullptr;
}

void * Tracer::eventHandler(void* tracer) {
    auto t = (Tracer *) tracer;
    if (t == nullptr) {
        std::cerr << __FUNCTION__ << " : tracer is nullptr" << std::endl;
        return nullptr;
    }

    t->handleEvent();

    return nullptr;
}

void * Tracer::callbackHandler(void *tracer) {
    auto t = (Tracer *) tracer;
    if(t == nullptr){
        std::cerr << __FUNCTION__ << " : tracer is nullptr" << std::endl;
        return nullptr;
    }

    t->handleCallback();

    return nullptr;
}

void Tracer::handleCommand() {
    std::cout << __FUNCTION__ << " : start handling commands!" << std::endl;
    setState(STARTING);

    while(sem_wait(&_cmdsSem) == 0)
    {
        if(_state == STOPPED) break;

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
    setState(STOPPED);
}

void Tracer::handleEvent() {
    std::cout << __FUNCTION__ << " : start handling events!" << std::endl;

    int ret = pthread_create(&_callbackHandler, &_attr, &callbackHandler, this);
    if(ret != 0){
        std::cerr << __FUNCTION__ << " : failed to create callback handler : " << strerror(ret) << std::endl;
    }

    int wstatus;
    pid_t tid;
    // Wait until all child threads exit
    while((tid = waitpid(-1, &wstatus, WCONTINUED)) > 0)
    {
        // Ignore starterThread
        if(tid == _traceePid) continue;

        SpiedThread &thread = _spiedProgram.getSpiedThread(tid);

        if (WIFSTOPPED(wstatus)) {
            int signum = WSTOPSIG(wstatus);
            thread.setState(SpiedThread::STOPPED, signum);
            std::cout << __FUNCTION__ << " : thread " << tid << " stopped : SIG = " << signum << std::endl;

            switch (signum) {
                case SIGTRAP:
                    executeCallback([&thread, wstatus]{
                        thread.handleSigTrap(wstatus);
                    });
                    break;
                case SIGSEGV:
                    std::cerr << __FUNCTION__ <<" "<< tid <<" : segfault at " << (void*)thread.getRip() << std::endl;
                    break;
                case SIGSTOP:
                case SIGTERM:
                    break;
                default:
                    std::cerr << __FUNCTION__ <<" "<< tid << " : unexpect signal "<< signum << " at " << (void*)thread.getRip() << std::endl;
                    break;
            }
        } else if (WIFCONTINUED(wstatus)) {
            thread.setState(SpiedThread::RUNNING);
            std::cout << __FUNCTION__ << " : thread " << tid << " resumed!" << std::endl;
        } else if (WIFEXITED(wstatus)) {
            thread.setState(SpiedThread::TERMINATED, WEXITSTATUS(wstatus));
            std::cout << __FUNCTION__ << " : thread " << tid << " exits with code " << WEXITSTATUS(wstatus)
                      << std::endl;
        } else if (WIFSIGNALED(wstatus)) {
            thread.setState(SpiedThread::TERMINATED, WTERMSIG(wstatus));
            std::cout << __FUNCTION__ << " : thread " << tid << " exits with signal " << WTERMSIG(wstatus)
                      << std::endl;
        }
    }

    setState(STOPPED);
    std::cout << __FUNCTION__ << ": stop handling events!" << std::endl;

    // Wake up command and callback handlers
    sem_post(&_callbackSem);
    sem_post(&_cmdsSem);
}

void Tracer::handleCallback() {
    while(sem_wait(&_callbackSem) == 0){

        _callbackMutex.lock();
        while(!_callbacks.empty()){
            auto& callback = _callbacks.front();
            _callbackMutex.unlock();

            callback();

            _callbackMutex.lock();
            _callbacks.pop();
        }
        _callbackMutex.unlock();

        if(_state == STOPPED) break;
    }
}

void Tracer::executeCallback(std::function<void()>&& callback) {
    _callbackMutex.lock();
    _callbacks.push(callback);
    _callbackMutex.unlock();

    sem_post(&_callbackSem);
}

void Tracer::start() {
    if(_state == STARTING) {
        setState(TRACING);
        SpiedThread& sp  = _spiedProgram.getSpiedThread(_traceeTid);
    }
    else{
        std::cout << __FUNCTION__ << " : tracer has already been started " << std::endl;
    }
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

pid_t Tracer::getTraceePid() const {
    return _traceePid;
}

void Tracer::setState(Tracer::E_State state) {
    _stateMutex.lock();
    _state = state;
    _stateMutex.unlock();
    _stateCV.notify_all();
}

Tracer::~Tracer() {
    if(_state != STOPPED) tkill(_traceePid, SIGKILL);

    pthread_join(_evtHandler, nullptr);
    pthread_join(_callbackHandler, nullptr);
    pthread_join(_cmdHandler, nullptr);
    pthread_join(_starter, nullptr);

    pthread_attr_destroy(&_attr);

    sem_destroy(&_cmdsSem);
    sem_destroy(&_callbackSem);

    std::cout<< __FUNCTION__ << std::endl;
}
