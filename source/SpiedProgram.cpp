#include <iostream>
#include <sys/wait.h>

#include "SpiedProgram.h"

SpiedProgram::~SpiedProgram(){
    _breakPoints.clear();
    _spiedThreads.clear();

   _eventListener.join();
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
    DynamicModule* spiedModule;
    DynamicNamespace* curNamespace = getSpyLoader().getCurrentNamespace();


    if(curNamespace == nullptr){
        error_log("Cannot find the current namespace");
        return false;
    }

    spiedModule = _spiedNamespace.load(libName);
    if(spiedModule == nullptr){
        error_log("Cannot load " << libName);
        return false;
    }

    bool isRelinked = curNamespace->iterateOverModule([spiedModule](DynamicModule& dynModule){
        try {
            dynModule.relink(*spiedModule);
        }
        catch(std::invalid_argument& e) {
            error_log("failed to relink " << dynModule.getName() << " -> " << spiedModule->getName() << " (" << e.what() << ")");
            return false;
        }
        return true;
    });

    if(!isRelinked)
        error_log("The relinking failed and may be partially achieved");

    return isRelinked;
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
        uint16_t ptraceEvent = 0;

        if (tid == _pid) continue;

        if (WIFSTOPPED(wstatus)) {
            state = SpiedThread::STOPPED;
            signal = WSTOPSIG(wstatus);

            if(signal == SIGTRAP) {
                ptraceEvent = (wstatus >> 16);
                /*
                if(_ptraceEvent != PTRACE_EVENT_STOP) {
                    if(tracer.commandPTrace(true, PTRACE_GETEVENTMSG, _tid, nullptr, &_ptraceEventMsg) == -1) {
                        error_log("PTRACE_GETEVENTMSG failed : " << strerror(errno));
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
            error_log("Unknown wstatus " << std::hex << wstatus);
        }

        auto threadIt = std::find_if(_spiedThreads.begin(), _spiedThreads.end(),
                                   [tid](auto& st) { return *st == tid; });

        if(threadIt == _spiedThreads.end()){
            info_log("New thread (" << tid << ") detected");

            auto& spiedThread = *_spiedThreads.emplace_back(
                    std::make_unique<SpiedThread>(_tracer, _callbackHandler, tid));

            if(_onThreadCreation){
                _callbackHandler.executeCallback([&spiedThread, this]{
                    _threadCreationMutex.lock();
                    _onThreadCreation(spiedThread);
                    _threadCreationMutex.unlock();
                });
            }
        } else if(!(*threadIt)->handleEvent(state, signal, status, ptraceEvent)) {
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




