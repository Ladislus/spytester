#ifndef SPYTESTER_WRAPPEDFUNCTION_H
#define SPYTESTER_WRAPPEDFUNCTION_H

#include <cstdint>
#include <string>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/ptrace.h>
#include <cstring>

#include "Tracer.h"
#include "DynamicLinker.h"
#include "Meta.h"

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
    Wrapper& _wrapper;
    void* _handle;
    void* _relaAddr;

public:
    WrappedFunction(Tracer& tracer, void* fptr, void *handle);

    void setWrapper(FctType&& wrapper);
    bool wrapping(bool active);

    ~WrappedFunction() override;
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
    if(wrappers.available.empty()){
        std::cerr << __FUNCTION__ << " : no available wrapper for "<< (void*)faddr << std::endl;
    }

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
            std::cerr << "WrapperFctPtr : " << e.what() << std::endl;
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
WrappedFunction<faddr>::WrappedFunction(Tracer& tracer, void* fptr, void *handle):
_tracer(tracer), _relaAddr(nullptr), _handle(handle), _wrapper(getWrapper((FctPtrType)fptr)){
    struct link_map* lm;
    if(dlinfo(_handle, RTLD_DI_LINKMAP, &lm) == -1){
        std::cerr << __FUNCTION__ << " : dlinfo failed for " << handle << " : " << dlerror() << std::endl;
        throw std::invalid_argument("Cannot get dynamic linker info");
    }

    const Elf64_Addr baseAddr = lm->l_addr;

    std::vector<uint64_t> dynEntries;
    const Elf64_Rela* dynRela = getDynEntry(lm, DT_RELA, dynEntries) == 1 ?
                                (Elf64_Rela*)dynEntries[0] : nullptr;
    const Elf64_Rela* pltRela = getDynEntry(lm, DT_JMPREL, dynEntries) == 1 ?
                                (Elf64_Rela*)dynEntries[0] : nullptr;
    const size_t dynRelaSize  = getDynEntry(lm, DT_RELASZ, dynEntries) == 1 ?
                                dynEntries[0] : 0U;
    const size_t pltRelaSize  = getDynEntry(lm, DT_PLTRELSZ, dynEntries) == 1 ?
                                dynEntries[0] : 0U;

    auto processRelaTable = [this, baseAddr] (const Elf64_Rela relaTable[], const size_t size){
        if(relaTable != nullptr){
            for(uint32_t idx = 0; idx < size/sizeof(Elf64_Rela); idx++){
                if ((ELF64_R_TYPE(relaTable[idx].r_info) == R_X86_64_GLOB_DAT) ||
                    (ELF64_R_TYPE(relaTable[idx].r_info) == R_X86_64_JUMP_SLOT))
                {
                    const auto relaAddr = (uint64_t *) (relaTable[idx].r_offset + baseAddr);
                    if (*relaAddr == (uint64_t)_wrapper.wrappedFunction) {
                        _relaAddr = relaAddr;
                    }
                }
            }
        }
    };

    processRelaTable(dynRela, dynRelaSize);
    processRelaTable(pltRela, pltRelaSize);

    if(_relaAddr == nullptr) {
        std::cerr << __FUNCTION__ << " : failed to find function (" << (void*)faddr
                  << ") in .rela.dyn and .rela.plt for " << handle << std::endl;
        std::invalid_argument("Cannot find function in relocation table");
    }
}

template<auto faddr>
bool WrappedFunction<faddr>::wrapping(bool active){
    bool res = true;
    void* addr = active ? (void*)_wrapper.staticWrapper : (void*)_wrapper.wrappedFunction;

    if(_relaAddr != nullptr){
        if (_tracer.commandPTrace(true, PTRACE_POKEDATA, _tracer.getTraceePid(), _relaAddr, addr) == -1) {
            std::cout << "WrappedFunction::wrap : PTRACE_POKEDATA failed : " << strerror(errno) << std::endl;
            res = false;
        }
    }

    return res;
}

template<auto faddr>
WrappedFunction<faddr>::~WrappedFunction(){
    _tracer.commandPTrace(false, PTRACE_POKEDATA, _tracer.getTraceePid(), _relaAddr, (void*)_wrapper.wrappedFunction);
    releaseWrapper(_wrapper);
}

#endif //SPYTESTER_WRAPPEDFUNCTION_H
