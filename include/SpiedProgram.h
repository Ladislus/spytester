#ifndef SPYTESTER_SPIEDPROGRAM_H
#define SPYTESTER_SPIEDPROGRAM_H


#include "SpiedThread.h"
#include "Breakpoint.h"
#include "Tracer.h"
#include "WrappedFunction.h"
#include "WatchPoint.h"

#include <sys/mman.h>
#include <unistd.h>

#include <string>
#include <map>
#include <set>
#include <vector>


struct ProgParam
{
    void *entryPoint;
    uint64_t argc;
    const char **argv;
    char **envp;
};

class SpiedProgram {
    friend Tracer;

private:
    const std::string _progName;
    std::vector<std::string> _argvStr;
    std::vector<const char*> _argv;

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
    std::mutex _callbackMutex;
    SpiedThread& getSpiedThread(pid_t tid);

    static void defaultOnAddThread(SpiedThread& spiedThread);
    static void defaultOnRemoveThread(SpiedThread& spiedThread);
public:
    template<typename ...ARGS>
    explicit SpiedProgram(std::string &&progName, ARGS ...args);

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
    WrappedFunction<faddr>* wrapFunction(std::string&& binName);

    template<auto faddr>
    void unwrapFunction(std::string&& binName);

    void start();
    void resume();
    void stop();
    void terminate();
};

static const size_t stackSize = 1<<23;

template<typename ... ARGS>
SpiedProgram::SpiedProgram(std::string &&progName, ARGS ... args)
        : _progName(progName), _onThreadStart(defaultOnAddThread),
          _onThreadExit(defaultOnRemoveThread), _lmid(0)
{
    (_argvStr.emplace_back(args) , ...); // fold expression

    _argv.push_back(_progName.c_str());
    for(const auto& str : _argvStr){
        _argv.push_back(str.c_str());
    }

    _progParam.argv = &_argv[0];
    _progParam.argc = _argv.size();
    _progParam.envp = environ;

    _handle = dlmopen(LM_ID_NEWLM, _progName.c_str(), RTLD_NOW);
    if (_handle == nullptr) {
        std::cerr << __FUNCTION__ << " : dlopen failed for " << _progName << " : " << dlerror() << std::endl;
        throw std::invalid_argument("Invalid program name");
    }

    if (dlinfo(_handle, RTLD_DI_LMID, &_lmid) == -1){
        std::cerr << __FUNCTION__ << " : dlinfo failed RTLD_DI_LMID : " << dlerror() << std::endl;
        throw std::invalid_argument("Failed to get new link map id");
    }

    struct link_map* lm;
    if (dlinfo(_handle, RTLD_DI_LINKMAP, &lm) == -1) {
        std::cerr << __FUNCTION__ << " : dlinfo failed RTLD_DI_LINKMAP : " << dlerror() << std::endl;
        throw std::invalid_argument("Failed to get link map ");
    }

    auto elfHdr = (Elf64_Ehdr*)lm->l_addr;
    Elf64_Addr baseAddr = lm->l_addr;
    _progParam.entryPoint = (void*)(baseAddr + elfHdr->e_entry);

    _stack = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (_stack == MAP_FAILED) {
        std::cerr << __FUNCTION__ << " : stack allocation failed : " << strerror(errno) << std::endl;
        dlclose(_handle);
        throw std::invalid_argument("Cannot allocate stack");
    }

    _tracer = new Tracer(*this);
}

template<auto faddr>
WrappedFunction<faddr>* SpiedProgram::wrapFunction(std::string &&binName) {
    WrappedFunction<faddr>* wrappedFunction = nullptr;

    void* handle = dlmopen(_lmid, binName.c_str(), RTLD_NOLOAD | RTLD_NOW);

    if(handle == nullptr) {
        std::cerr << __FUNCTION__ << " : dlopen failed for " << handle << " : " << dlerror() << std::endl;
        return nullptr;
    }

    auto it = _wrappedFunctions.find({(void*)faddr, handle});
    if( it == _wrappedFunctions.end() ){
        void* addr   = getDynSymbolAddrIn((void*)faddr, _lmid);

        if (addr != nullptr){
            auto uniquePtr = std::make_unique<WrappedFunction<faddr>>(*_tracer, addr, handle);
            wrappedFunction = uniquePtr.get();
            _wrappedFunctions[{(void*)faddr, handle}] = std::move(uniquePtr);
        }
    } else {
        wrappedFunction = dynamic_cast<WrappedFunction<faddr>*>(it->second.get());
    }

    return wrappedFunction;
}

template<auto faddr>
void SpiedProgram::unwrapFunction(std::string &&binName) {
    void* handle = dlmopen(_lmid, binName.c_str(), RTLD_NOLOAD | RTLD_NOW);

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
