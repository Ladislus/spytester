#ifndef SPYTESTER_SPIEDPROGRAM_H
#define SPYTESTER_SPIEDPROGRAM_H


#include "SpiedThread.h"
#include "Breakpoint.h"
#include "Tracer.h"
#include "WrappedFunction.h"
#include "WatchPoint.h"

#include <string>
#include <map>
#include <set>
#include <vector>


struct ProgParam
{
    void *entryPoint;
    uint64_t argc;
    char *argv;
    char *envp;
};

class SpiedProgram {
    friend Tracer;

private:
    const std::string _progName;
    void* _handle;
    Lmid_t _lmid;
    void* _stack;
    ProgParam _progParam{};

    Tracer* _tracer;
    std::vector<std::unique_ptr<SpiedThread>> _spiedThreads;
    std::vector<std::unique_ptr<BreakPoint>> _breakPoints;
    std::map<std::pair<void*, void*>, std::unique_ptr<AbstractWrappedFunction>> _wrappedFunctions;

    static uint32_t breakPointCounter;

    std::function<void(SpiedThread&)> _onThreadStart;
    std::function<void(SpiedThread&)> _onThreadExit;
    SpiedThread& getSpiedThread(pid_t tid);

public:
    SpiedProgram(std::string &&progName, int argc, char *argv, char *envp);

    ~SpiedProgram();

    bool relink(std::string&& libName);

    void setOnThreadStart(std::function<void(SpiedThread &)>&& onThreadStart);

    ProgParam* getProgParam();
    const std::string& getProgName();
    char* getStackTop();

    BreakPoint* getBreakPointAt(void* addr);
    BreakPoint* createBreakPoint(std::string&& symbName);
    BreakPoint* createBreakPoint(void* addr,
                                 std::string&& name = "BreakPoint" + std::to_string(breakPointCounter++));

    template<auto faddr>
    AbstractWrappedFunction* wrapFunction(std::string&& binName);

    template<auto faddr>
    void unwrapFunction(std::string&& binName);

    void start();
    void resume();
    void stop();
    void terminate();
};

template<auto faddr>
AbstractWrappedFunction* SpiedProgram::wrapFunction(std::string &&binName) {
    AbstractWrappedFunction* wrappedFunction = nullptr;
    void* handle = dlmopen(_lmid, binName.c_str(), RTLD_NOLOAD | RTLD_NOW);

    if(handle == nullptr){
        std::cerr << __FUNCTION__ << " : dlopen failed for " << handle << " : " << dlerror() << std::endl;
    } else try {
        _wrappedFunctions.try_emplace(
                std::make_pair((void*)faddr, handle),
                std::make_unique<WrappedFunction<faddr>>(*_tracer, handle)
        );
        wrappedFunction = _wrappedFunctions[std::make_pair((void*)faddr, handle)].get();

    } catch(const std::invalid_argument& e) {
        std::cerr << __FUNCTION__ <<" : WrappedFunction construction failed : " << e.what() << std::endl;
    }

    return wrappedFunction;
}

template<auto faddr>
void SpiedProgram::unwrapFunction(std::string &&binName) {
    void* handle = dlopen(binName.c_str(), RTLD_NOLOAD | RTLD_NOW);

    if(_handle == nullptr){
        std::cerr << __FUNCTION__ << " : dlopen failed for " << handle << " : " << dlerror() << std::endl;
    } else {
        auto it = _wrappedFunctions.find(faddr, handle);
        if(it != _wrappedFunctions.end()){
            _wrappedFunctions.erase(it);
        }
    }
}

#endif //SPYTESTER_SPIEDPROGRAM_H
