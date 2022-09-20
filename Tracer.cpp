//
// Created by baptiste on 13/09/22.
//

#include <sys/ptrace.h>
#include <iostream>
#include <cstring>
#include <wait.h>
#include <unistd.h>
#include <sys/mman.h>
#include "Tracer.h"
#include "SpiedProgram.h"

#include <sys/user.h>
#include <dlfcn.h>

const size_t Tracer::stackSize =  1<<23;

std::map<pid_t, Tracer*> Tracer::tracers;

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

int Tracer::tracerMain(void * tracer) {
    auto t = (Tracer*) tracer;
    SpiedProgram& s = t->_spiedProgram;

    t->_mainPid = clone(starter, s.getStackTop(),
                        CLONE_FS | CLONE_VM | CLONE_FILES , s.getProgParam());
    if(t->_mainPid == -1)
    {
        std::cerr << __FUNCTION__ <<" : clone failed : "<< strerror(errno) <<std::endl;
        return 1;
    }

    pid_t pid;
    int wstatus;
    while((pid = waitpid(t->_mainPid, &wstatus, WNOHANG)) <= 0);

    if (WIFSTOPPED(wstatus)){
        if(ptrace(PTRACE_SETOPTIONS, t->_mainPid, NULL, PTRACE_O_TRACECLONE) == -1)
        {
            std::cerr << __FUNCTION__ <<" : PTRACE_O_TRACECLONE failed : "<< strerror(errno) <<std::endl;
        }
        else
        {
            t->_spiedThreads.emplace(std::piecewise_construct,
                                     std::forward_as_tuple(pid),
                                     std::forward_as_tuple(*t, pid));
            std::cout<< __FUNCTION__ <<" : "<< s.getProgName() <<" ready to run!" << std::endl;
        }
    }

    // add current to _tracer map
    tracers[t->_tracerPid] = t;

    if (signal( SIGUSR1, sigHandler) == SIG_ERR)
    {
        std::cerr << __FUNCTION__ <<" : signal SIGUSR1 failed" << std::endl;
    }

    while((pid = wait(&wstatus)) > 0)
    {
        auto it = t->_spiedThreads.find(pid);
        SpiedThread* thread = &(it->second);

        if(it == t->_spiedThreads.end())
        {
            std::cout << __FUNCTION__ << " : new thread " << pid <<" detected!"<<std::endl;
            thread = &(t->_spiedThreads.emplace(std::piecewise_construct,
                                              std::forward_as_tuple(pid),
                                              std::forward_as_tuple(*t, pid)).first->second);
        }

        if(WIFSTOPPED(wstatus))
        {
            thread->setRunning(false);
            int signum = WSTOPSIG(wstatus);
            std::cout << __FUNCTION__ << " : thread " << pid <<" stopped : SIG = " << signum<< std::endl;
            switch(signum)
            {
                case SIGTRAP:
                    t->handleSigTrap(*thread);
                    break;
                case SIGSTOP:
                    break;
                default:
                    std::cerr << __FUNCTION__ << " : unexpected signal" << std::endl;
            }
        }
        else if(WIFCONTINUED(wstatus))
        {
            thread->setRunning(true);
            std::cout << __FUNCTION__  <<" : thread " << pid <<" resumed!" << std::endl;
        }
        else if(WIFEXITED(wstatus))
        {
            thread->setRunning(false);
            std::cout << __FUNCTION__ <<" : thread "<<pid<<" exits with code "<<WEXITSTATUS(wstatus)<<std::endl;
        }
        else if(WIFSIGNALED(wstatus))
        {
            thread->setRunning(false);
            std::cout << __FUNCTION__<<" : thread "<<pid<<" exits with signal "<<WTERMSIG(wstatus)<<std::endl;
        }
    }

    std::cout << __FUNCTION__ << " exit" << std::endl;

    return 0;
}

void Tracer::sigHandler(int signum) {
    Tracer* const t = tracers[getpid()];

    if(t == nullptr)
    {
        std::cerr << __FUNCTION__ << " : unexpected pid " << getpid() << std::endl;
        return;
    }
    if(signum != SIGUSR1)
    {
        std::cerr << __FUNCTION__ << " : unexpected signal " << signum << std::endl;
        return;
    }

    //Get next command to execute
    while(1)
    {
        Command *cmd;

        t->_cmdsMutex.lock();
        if(!t->_commands.empty()) {
            cmd = t->_commands.front();
            t->_commands.pop();
            t->_cmdsMutex.unlock();
        }
        else
        {
            t->_cmdsMutex.unlock();
            break;
        }

        std::cout << __FUNCTION__ << " : command received " << std::endl;
        cmd->execute();
    }
}

Tracer::Tracer(SpiedProgram &spiedProgram)
:_spiedProgram(spiedProgram)
{
    _stack = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (_stack == MAP_FAILED) {
        std::cerr << __FUNCTION__ << " : stack allocation failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("Cannot allocate stack");
    }

    char* stackTop = (char *) _stack + stackSize;
    _tracerPid = clone(tracerMain, stackTop,
                 CLONE_FS | CLONE_VM | CLONE_FILES, this);
    if(_tracerPid == -1)
    {
        std::cerr << __FUNCTION__ << " : clone failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("Cannot launch _tracer thread");
    }
}

void Tracer::command(Command *cmd) {
    if(cmd != nullptr)
    {
        _cmdsMutex.lock();
        _commands.push(cmd);
        _cmdsMutex.unlock();
        kill(_tracerPid, SIGUSR1);
    }
}

void Tracer::stop() {

}

SpiedThread &Tracer::getMainThread() {
    return _spiedThreads.at(_mainPid);
}

Tracer::~Tracer() {
    std::cout<< __FUNCTION__ << std::endl;
}

pid_t Tracer::getMainPid() const {
    return _mainPid;
}

void Tracer::handleSigTrap(SpiedThread &spiedThread) {
    struct user_regs_struct regs;

    if(ptrace(PTRACE_GETREGS, spiedThread.getPid(), NULL, &regs))
    {
        std::cerr << __FUNCTION__ << " : PTRACE_GETREGS failed : " << strerror(errno) << std::endl;
        return;
    }

    BreakPoint* const bp = _spiedProgram.getBreakPointAt(regs.rip-1);
    if(bp != nullptr)
    {
        bp->hit(spiedThread);
    }
    else
    {
        Dl_info info;
        if (dladdr((void*)(regs.rip-1), &info) == 0)
        {
            std::cerr << __FUNCTION__ <<" : unexpected SIGTRAP for thread " << spiedThread.getPid() << std::endl;
        }
        else
        {
            std::cout << __FUNCTION__ <<" : SIGTRAP at "<<info.dli_sname <<" ("<<info.dli_fname<<")"<<std::endl;
        }


    }
}

pid_t Tracer::getPid() const {
    return _tracerPid;
}
