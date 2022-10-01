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

    std::cout << __FUNCTION__ << " : start ! : " << getpid() << std::endl;

    if( raise(SIGTRAP) != 0)
    {
        std::cerr<<__FUNCTION__<<" : ptrace failed : " << strerror(errno) << std::endl;
        std::exit(1);
    }

    _asm_starter(progParam->entryPoint, progParam->argc, progParam->argv, progParam->envp);

    return 0;
}

int Tracer::tracerMain(void * tracer) {
    auto t = (Tracer*) tracer;
    SpiedProgram& s = t->_spiedProgram;

    t->_mainPid = clone(starter, s.getStackTop(),
                        CLONE_FS | CLONE_VM | CLONE_FILES | SIGCHLD, s.getProgParam());
    if(t->_mainPid == -1)
    {
        std::cerr << __FUNCTION__ <<" : clone failed : "<< strerror(errno) <<std::endl;
        return 1;
    }

    int wstatus;
    if(waitpid(t->_mainPid, &wstatus, 0) == -1)
    {
        std::cerr << "waitpid failed : " << strerror(errno) << std::endl;
    }

    if (WIFSTOPPED(wstatus)){
        if(ptrace(PTRACE_SETOPTIONS, t->_mainPid, NULL, PTRACE_O_TRACECLONE) == -1)
        {
            std::cerr << __FUNCTION__ <<" : PTRACE_O_TRACECLONE failed : "<< strerror(errno) <<std::endl;
        }
        else
        {
            (void) t->_spiedProgram.onThreadStart(t->_mainPid);
            std::cout<< __FUNCTION__ <<" : "<< s.getProgName() <<" ready to run!" << std::endl;
        }
    }

    if (clone(eventHandler, (char *) t->_cmdrStack + stackSize,
              CLONE_FS | CLONE_VM | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD, t) == -1)
    {
        std::cerr << __FUNCTION__ <<" : failed to clone eventHandler : " << strerror(errno) << std::endl;
    }

    t->handleCommand();

    return 0;
}

void Tracer::handleEvent() {
    std::cout << __FUNCTION__ << " : start handling events!" << std::endl;

    int wstatus;
    pid_t tid;
    while((tid = wait(&wstatus)) > 0)
    {
        SpiedThread* thread = _spiedProgram.getSpiedThread(tid);

        if(thread == nullptr)
        {
            std::cout << __FUNCTION__ << " : new thread " << tid <<" detected!"<<std::endl;
            thread = _spiedProgram.onThreadStart(tid);
        }

        if(WIFSTOPPED(wstatus))
        {
            thread->setRunning(false);
            int signum = WSTOPSIG(wstatus);
            std::cout << __FUNCTION__ << " : thread " << tid <<" stopped : SIG = " << signum<< std::endl;
            switch(signum)
            {
                case SIGTRAP:
                    thread->handleSigTrap();
                    break;
                case SIGSTOP:
                    break;
                default:
                    thread->resume(signum);
                    break;
            }
        }
        else if(WIFCONTINUED(wstatus))
        {
            thread->setRunning(true);
            std::cout << __FUNCTION__  <<" : thread " << tid <<" resumed!" << std::endl;
        }
        else if(WIFEXITED(wstatus))
        {
            thread->setRunning(false);
            std::cout << __FUNCTION__ <<" : thread "<< tid <<" exits with code "<<WEXITSTATUS(wstatus)<<std::endl;
        }
        else if(WIFSIGNALED(wstatus))
        {
            thread->setRunning(false);
            std::cout << __FUNCTION__<<" : thread "<< tid <<" exits with signal "<<WTERMSIG(wstatus)<<std::endl;
        }
    }

    _isTracing = false;
    sem_post(&_cmdsSem);
    std::cout << __FUNCTION__ << ": stop handling events!" << std::endl;
}

int Tracer::eventHandler(void* tracer) {
    auto t = (Tracer *) tracer;
    if (t == nullptr) {
        std::cerr << __FUNCTION__ << " : unexpected pid " << getpid() << std::endl;
        return 1;
    } else {
        t->handleEvent();
    }

    return 0;
}

void Tracer::handleCommand() {
    //Get next command to execute
    std::cout << __FUNCTION__ << " : start handling commands!" << std::endl;
    _tracerTid = gettid();

    while(_isTracing)
    {
        if(sem_wait(&_cmdsSem) != 0)
            break;

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
}

Tracer::Tracer(SpiedProgram &spiedProgram)
:_spiedProgram(spiedProgram), _isTracing(true)
{
    if(sem_init(&_cmdsSem, 0, 0) == -1)
    {
        std::cerr << __FUNCTION__ << " : semaphore initialization failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("invalid sem init");
    }

    _cmdrStack = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (_cmdrStack == MAP_FAILED) {
        std::cerr << __FUNCTION__ << " : stack allocation failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("Cannot allocate stack");
    }
    _waitrStack = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (_waitrStack == MAP_FAILED) {
        std::cerr << __FUNCTION__ << " : stack allocation failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("Cannot allocate stack");
    }

    char* stackTop = (char *) _waitrStack + stackSize;
    _tracerPid = clone(tracerMain, stackTop,
                 CLONE_FS | CLONE_VM | CLONE_FILES, this);
    if(_tracerPid == -1)
    {
        std::cerr << __FUNCTION__ << " : clone failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("Cannot launch _tracer thread");
    }
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
    sem_destroy(&_cmdsSem);
    std::cout<< __FUNCTION__ << std::endl;
}

pid_t Tracer::getMainTid() const {
    return _mainPid;
}

bool Tracer::isTracerThread() const {
    return gettid() == _tracerTid;
}
