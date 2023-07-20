#ifndef SPYTESTER_WRAPPEDFUNCTION_H
#define SPYTESTER_WRAPPEDFUNCTION_H


#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <string>
#include <sys/ptrace.h>

#include "Tracer.h"
#include "Meta.h"
#include "Logger.h"

#ifndef WRAPPER_MAX_NB
#define WRAPPER_MAX_NB 10
#endif

struct AbstractWrappedFunction{
    virtual ~AbstractWrappedFunction() = default;
};

template<auto faddr>
class WrappedFunction : public AbstractWrappedFunction{

    using FctPtrType = decltype(faddr);
    using FctType = decltype(std::function(std::declval<FctPtrType>()));

public:
    WrappedFunction(Tracer& tracer, DynamicNamespace& dynamicNamespace, std::string binName);

    void setWrapper(FctType&& wrapper);
    bool wrapping(bool active);

    ~WrappedFunction() override;

private:
    struct Wrapper {
        FctPtrType staticWrapper;
        FctPtrType wrappedFunction;
        FctType dynamicWrapper;
        std::mutex wrapperMutex;
        Wrapper() : wrappedFunction(faddr), staticWrapper(nullptr){}
    };

    struct Wrappers {
        std::array<Wrapper, WRAPPER_MAX_NB> wrappers;
        std::vector<Wrapper*> available;

        Wrappers(){
            constexpr_for<0, WRAPPER_MAX_NB, 1U>([this](auto idx){
                wrappers[idx].staticWrapper = getStaticWrapper<idx>(faddr);
                available.push_back(&wrappers[idx]);
            });
        }

        Wrapper& operator[](uint32_t idx){ return wrappers[idx];}
    };

    static std::mutex freeWrappersMutex;
    static Wrappers wrappers;

    static Wrapper& getWrapper(FctPtrType wrappedFunction);
    static void releaseWrapper(Wrapper& wrapper);

    template<uint32_t idx, typename TRET, typename ... TARGS>
    static FctPtrType getStaticWrapper(TRET(*fct)(TARGS ...));

    Tracer& _tracer;
    DynamicNamespace& _spiedNamespace;
    Wrapper& _wrapper;
    std::string _binName;
    void* _relaAddr;
};

template<auto faddr>
void WrappedFunction<faddr>::releaseWrapper(WrappedFunction::Wrapper &wrapper) {
    std::lock_guard lk(freeWrappersMutex);

        wrapper.wrapperMutex.lock();
    wrapper.dynamicWrapper = FctType();
    wrapper.wrapperMutex.unlock();

    wrappers.available.push_back(&wrapper);
}

template<auto faddr>
typename WrappedFunction<faddr>::Wrapper &WrappedFunction<faddr>::getWrapper(FctPtrType wrappedFunction) {
    std::lock_guard lk(freeWrappersMutex);
    if(wrappers.available.empty())
        error_log("No available wrapper for "<< (void*)faddr);


    Wrapper& wrapper = *wrappers.available.back();
    wrapper.wrappedFunction = wrappedFunction;
    wrappers.available.pop_back();

    return wrapper;
}

template<auto faddr>
std::mutex WrappedFunction<faddr>::freeWrappersMutex;

template<auto faddr>
typename WrappedFunction<faddr>::Wrappers WrappedFunction<faddr>::wrappers;

template<auto faddr>
template<uint32_t idx, typename TRET, typename ... TARGS>
typename WrappedFunction<faddr>::FctPtrType
WrappedFunction<faddr>::getStaticWrapper(TRET(*fct)(TARGS ...)) {
    return [](TARGS ... args) noexcept {
        std::lock_guard lk(wrappers[idx].wrapperMutex);

        try {
            return wrappers[idx].dynamicWrapper(args ...);
        } catch (const std::bad_function_call& e){
            error_log("WrapperFctPtr : " << e.what());
            return wrappers[idx].wrappedFunction(args ...);
        }
    };
}

template<auto faddr>
void WrappedFunction<faddr>::setWrapper(FctType&& wrapper){
    std::lock_guard lk(_wrapper.wrapperMutex);
    _wrapper.dynamicWrapper = wrapper;
}

template<auto faddr>
WrappedFunction<faddr>::WrappedFunction(Tracer& tracer, DynamicNamespace& dynamicNamespace, std::string binName):
    _tracer(tracer), 
    _spiedNamespace(dynamicNamespace),
    _relaAddr(nullptr), 
    _binName(std::move(binName)),
    _wrapper(getWrapper((FctPtrType)_spiedNamespace.convertDynSymbolAddr((void*)faddr))) {
    if(!this->_wrapper.wrappedFunction) {
        error_log("Failed to find function (" << (void*)faddr << ") definition in spied namespace");
        std::invalid_argument("Cannot find function definition");
    }

    DynamicModule* dynamicModule = _spiedNamespace.load(_binName);
    if(!dynamicModule) {
        error_log(_binName << " cannot be found or loaded in the spied namespace");
        std::invalid_argument("Cannot find "+ _binName +" in the spied namespace");
    }

    std::string mangledFunctionName = DynamicModule::getMangledName((void*)faddr);

    auto findRela = [this, &mangledFunctionName](uint32_t type, const std::string& name, uint64_t* addr){
        if((type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT) && (name == mangledFunctionName))
        {
            _relaAddr = addr;
            return false;
        }
        return true;
    };

    dynamicModule->iterateOverRelocations(findRela);

    if(!_relaAddr) {
        error_log("Failed to find for " << _wrapper.wrappedFunction << " in relocation table of " << _binName);
        std::invalid_argument("Cannot find function in relocation table");
    }
}

template<auto faddr>
bool WrappedFunction<faddr>::wrapping(bool active){
    void* addr = active ? (void*)_wrapper.staticWrapper : (void*)_wrapper.wrappedFunction;

    if(_relaAddr != nullptr){
        auto futureRes = _tracer.writeWord(_relaAddr, (uint64_t)addr);
        auto res = futureRes.get();
        if ( res.first == -1) {
            error_log("WriteWord failed : " << strerror(res.second));
            return false;
        }
    }
    return true;
}

template<auto faddr>
WrappedFunction<faddr>::~WrappedFunction() {
    this->_tracer.writeWord(_relaAddr, (uint64_t)_wrapper.wrappedFunction);
    releaseWrapper(_wrapper);
}

#endif //SPYTESTER_WRAPPEDFUNCTION_H
