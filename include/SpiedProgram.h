#ifndef SPYTESTER_SPIEDPROGRAM_H
#define SPYTESTER_SPIEDPROGRAM_H


#include "SpiedThread.h"
#include "Breakpoint.h"
#include "Tracer.h"
#include "WrappedFunction.h"
#include "WatchPoint.h"
#include "CallbackHandler.h"
#include "DynamicNamespace.h"
#include "SpyLoader.h"

#include <sys/mman.h>
#include <unistd.h>

#include <string>
#include <map>
#include <set>
#include <vector>
#include <numeric>


class SpiedProgram {
private:
    std::vector<std::string> _argvStr;
    std::vector<const char*> _argv;

    pid_t _pid;

    CallbackHandler _callbackHandler;
    DynamicNamespace _spiedNamespace;
    Tracer _tracer;

    std::thread _eventListener;

    std::vector<std::unique_ptr<SpiedThread>> _spiedThreads;
    std::vector<std::unique_ptr<BreakPoint>> _breakPoints;
    std::map<
        std::pair<void*, std::string>,
        std::unique_ptr<AbstractWrappedFunction>
    > _wrappedFunctions;

    std::mutex _threadCreationMutex;
    std::function<void(SpiedThread&)> _onThreadCreation;

    void listenEvent();

public:
    template<typename ...ARGS>
    explicit SpiedProgram(const std::string &progName, ARGS ...args);

    ~SpiedProgram();

    void setThreadCreationCallback(const std::function<void(SpiedThread&)>&);

    bool relink(const std::string &libName);

    BreakPoint* createBreakPoint(void* addr, std::string&& name);

    template<auto faddr>
    WrappedFunction<faddr>* wrapFunction(const std::string& binName);
    template<auto faddr>
    void unwrapFunction(const std::string& binName);

    void start();
    void resume();
    void stop();
    void terminate();
};

static const size_t stackSize = 1<<23;

template<typename ... ARGS>
SpiedProgram::SpiedProgram(const std::string& progName, ARGS ... args) :
_argvStr({progName, args ...}),
_argv(std::accumulate( _argvStr.begin(), _argvStr.end(), std::vector<const char*>(),
                       [](auto& v, const std::string& s) {
                            v.push_back(s.c_str());
                            return v;
                       })),
_spiedNamespace((int)_argvStr.size(), _argv.data(), environ),
_tracer()
{
    _pid = _tracer.startTracing(_spiedNamespace);
}

template<auto faddr>
WrappedFunction<faddr>* SpiedProgram::wrapFunction(const std::string &binName) {
    WrappedFunction<faddr>* wrappedFunction;

    std::pair<void*, std::string> key((void*)faddr, binName);
    auto it = _wrappedFunctions.find(key);

    if( it == _wrappedFunctions.end() ){
        auto uniquePtr = std::make_unique<WrappedFunction<faddr>>(_tracer, _spiedNamespace, binName);
        wrappedFunction = uniquePtr.get();
        _wrappedFunctions[std::move(key)] = std::move(uniquePtr);
    } else {
        wrappedFunction = dynamic_cast<WrappedFunction<faddr>*>(it->second.get());
    }

    return wrappedFunction;
}

template<auto faddr>
void SpiedProgram::unwrapFunction(const std::string &binName) {

    auto it = _wrappedFunctions.find(faddr, binName);
    if(it != _wrappedFunctions.end()){
        _wrappedFunctions.erase(it);
    }

}

#endif //SPYTESTER_SPIEDPROGRAM_H
