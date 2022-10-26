//
// Created by baptiste on 10/09/22.
//

#ifndef SPYTESTER_SPIEDPROGRAM_H
#define SPYTESTER_SPIEDPROGRAM_H


#include "SpiedThread.h"
#include "Breakpoint.h"
#include "TracingCommand.h"
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
    void* _stack;
    ProgParam _progParam{};


    Tracer* _tracer;
    std::vector<std::unique_ptr<SpiedThread>> _spiedThreads;
    std::vector<std::unique_ptr<BreakPoint>> _breakPoints;

    static uint32_t breakPointCounter;

    void(*_onThreadStart)(SpiedThread& spiedThread);
    void(*_onThreadExit)(SpiedThread& spiedThread);
    SpiedThread& getSpiedThread(pid_t tid);

public:
    SpiedProgram(std::string &&progName, int argc, char *argv, char *envp);

    ~SpiedProgram();
    void setOnThreadStart(void(*onAddThread)(SpiedThread&) );

    ProgParam* getProgParam();
    const std::string& getProgName();
    char* getStackTop();

    BreakPoint* getBreakPointAt(void* addr);
    BreakPoint* createBreakPoint(std::string&& symbName);
    BreakPoint* createBreakPoint(void* addr,
                                 std::string&& name = "BreakPoint" + std::to_string(breakPointCounter));

    template<typename TRET, typename ... ARGS>
    WrappedFunction<TRET, ARGS ...>* createWrappedFunction(std::string&& binName, TRET (*function)(ARGS ...));

    void start();
    void resume();
    void stop();
    void terminate();
};

template<typename TRET, typename... ARGS>
WrappedFunction<TRET, ARGS...> *SpiedProgram::createWrappedFunction(std::string &&binName, TRET (*function)(ARGS...)) {
    // #TODO add wrapped function management in SpiedProgram
    return new WrappedFunction<TRET, ARGS...>(*_tracer, binName, function);
}

#endif //SPYTESTER_SPIEDPROGRAM_H
