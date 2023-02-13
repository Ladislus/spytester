#include "../include/SpiedProgram.h"

#include <iostream>
#include <sys/wait.h>


SpiedProgram::~SpiedProgram(){
    _breakPoints.clear();
    _spiedThreads.clear();

    _eventListener.join();

    std::cout << __FUNCTION__ << std::endl;
}

void SpiedProgram::start() {
    _eventListener = std::thread(&SpiedProgram::listenEvent, this);
}

void SpiedProgram::resume() {
    for(auto & spiedThread : _spiedThreads)
    {
        spiedThread->resume();
    }
}

void SpiedProgram::stop(){
    for(auto & spiedThread : _spiedThreads)
    {
        spiedThread->stop();
    }
}

void SpiedProgram::terminate() {
    if(!_spiedThreads.empty()){
        _spiedThreads.front()->terminate();
    }
}


// Exec Breakpoint Management
BreakPoint *SpiedProgram::createBreakPoint(void *addr, std::string &&name) {
    _breakPoints.emplace_back(std::make_unique<BreakPoint>(_tracer, _callbackHandler, std::move(name), addr));
    return _breakPoints.back().get();
}

bool SpiedProgram::relink(const std::string &libName) {
    return _dynamicLinker.relink(libName, _tracer);
}

void SpiedProgram::listenEvent() {
    int wstatus;

    pid_t tid;

    // Wait until all child threads exit
    while((tid = waitpid(-1, &wstatus, WCONTINUED)) > 0) {
        // Ignore starterThread
        SpiedThread::E_State state = SpiedThread::UNDETERMINED;
        int signal = 0;
        int status = 0;

        if (tid == _pid){
            std::cout << "loader ("<< _pid <<") : wstatus = "<< (void*)wstatus << std::endl;
            continue;
        }

        if (WIFSTOPPED(wstatus)) {
            state = SpiedThread::STOPPED;
            signal = WSTOPSIG(wstatus);

            if(signal == SIGTRAP && wstatus >> 16) {
                enum __ptrace_eventcodes ptraceEvent =
                        static_cast<enum __ptrace_eventcodes>(wstatus >> 16);
                /*
                if(_ptraceEvent != PTRACE_EVENT_STOP) {
                    if(tracer.commandPTrace(true, PTRACE_GETEVENTMSG, _tid, nullptr, &_ptraceEventMsg) == -1) {
                        std::cerr << __FUNCTION__ << " : PTRACE_GETEVENTMSG failed : " << strerror(errno) << std::endl;
                    }
                }*/
            }
        } else if (WIFCONTINUED(wstatus)) {
            state = SpiedThread::CONTINUED;
        } else if (WIFEXITED(wstatus)) {
            state = SpiedThread::EXITED;
            int status = WEXITSTATUS(wstatus);
        } else if (WIFSIGNALED(wstatus)) {
            state = SpiedThread::TERMINATED;
            signal = WTERMSIG(wstatus);
        } else {
            std::cerr << __FUNCTION__ << " : unknown wstatus : " << std::hex << wstatus << std::dec <<std::endl;
        }

        auto threadIt = std::find_if(_spiedThreads.begin(), _spiedThreads.end(),
                                   [tid](auto& st) { return *st == tid; });

        if(threadIt == _spiedThreads.end()){
            std::cout << __FUNCTION__ << " : New thread (" << tid << ") detected \n";

            auto& spiedThread = *_spiedThreads.emplace_back(
                    std::make_unique<SpiedThread>(_tracer, _callbackHandler, tid));

            if(_onThreadCreation){
                _callbackHandler.executeCallback([&spiedThread, this]{
                    _threadCreationMutex.lock();
                    _onThreadCreation(spiedThread);
                    _threadCreationMutex.unlock();
                });
            }
        } else if(!(*threadIt)->handleEvent(state, signal, status)) {
            uint64_t pc = (*threadIt)->getRip();
            auto breakPointIt = std::find_if(_breakPoints.begin(), _breakPoints.end(),
                                             [pc](auto& bp) { return *bp == (void*)(pc-1); });

            if(breakPointIt != _breakPoints.end())
                (*breakPointIt)->hit(**threadIt);
        }
    }

}

void SpiedProgram::setThreadCreationCallback(const std::function<void(SpiedThread&)>& callback) {
    _threadCreationMutex.lock();
    _onThreadCreation = callback;
    _threadCreationMutex.unlock();
}




