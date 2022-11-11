//
// Created by baptiste on 25/09/22.
//

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
class AbstractWrappedFunction{
public :
    virtual bool wrapping(bool active) =0;
    virtual ~AbstractWrappedFunction() = default;
};

template<auto faddr>
class WrappedFunction : public AbstractWrappedFunction{
    using FctPtrType = decltype(faddr);
    using FctType = decltype(std::function{std::declval<FctPtrType>()});

    static FctType _sWrapper;

    template< typename TRET, typename ... TARGS>
    static FctPtrType getWrapperFctPtr(TRET(*fct)(TARGS ...));

    Tracer& _tracer;
    void* _handle;
    void* _wrapperAddr;
    void* _pltAddr;
    void* _gotAddr;

public:
    static void setWrapper(FctType&& wrapper);

    WrappedFunction(Tracer& tracer, void *handle);
    bool wrapping(bool active) override;
    ~WrappedFunction() override;
};

template<auto faddr>
typename WrappedFunction<faddr>::FctType WrappedFunction<faddr>::_sWrapper;

template<auto faddr>
template<typename TRET, typename ... TARGS>
typename WrappedFunction<faddr>::FctPtrType
WrappedFunction<faddr>::getWrapperFctPtr(TRET(*fct)(TARGS ...)) {
    return [](TARGS ... args) noexcept {
        try {
            return _sWrapper(args ...);
        } catch (const std::bad_function_call& e){
            std::cerr << "WrapperFctPtr : " << e.what() << std::endl;
            return faddr(args ...);
        }
    };
}

template<auto faddr>
void WrappedFunction<faddr>::setWrapper(FctType&& wrapper){
    _sWrapper = wrapper;
}

template<auto faddr>
WrappedFunction<faddr>::WrappedFunction(Tracer& tracer, void *handle):
_tracer(tracer), _wrapperAddr((void*)getWrapperFctPtr(faddr)), _pltAddr(nullptr), _gotAddr(nullptr),
_handle(handle){
    std::cout << __FUNCTION__ << std::endl;
    struct link_map* lm;
    if(dlinfo(_handle, RTLD_DI_LINKMAP, &lm) == -1)
    {
        std::cerr << __FUNCTION__ << " : dlinfo failed for " << handle << " : " << dlerror() << std::endl;
        throw std::invalid_argument("Cannot get dynamic linker info");
    }

    const Elf64_Addr baseAddr = lm->l_addr;

    const Elf64_Rela* dynRela = nullptr;
    const Elf64_Rela* pltRela = nullptr;
    size_t dynRelaSize = 0;
    size_t pltRelaSize = 0;

    for (Elf64_Dyn* dynEntry = lm->l_ld; dynEntry->d_tag != DT_NULL; dynEntry++)
    {
        switch(dynEntry->d_tag)
        {
            case DT_RELA:
                dynRela = (Elf64_Rela *)dynEntry->d_un.d_ptr;
                break;
            case DT_RELASZ:
                dynRelaSize = dynEntry->d_un.d_val;
                break;
            case DT_JMPREL:
                pltRela = (Elf64_Rela *)dynEntry->d_un.d_ptr;
                break;
            case DT_PLTRELSZ:
                pltRelaSize = dynEntry->d_un.d_val;
                break;
            default:
                break;
        }
    }

    if(dynRela != nullptr)
    {
        for(uint32_t idx = 0; idx < dynRelaSize/sizeof(Elf64_Rela); idx++){
            if(ELF64_R_TYPE(dynRela[idx].r_info) == R_X86_64_GLOB_DAT) {
                const auto relaAddr = (uint64_t *) (dynRela[idx].r_offset + baseAddr);
                if (*relaAddr == (uint64_t)faddr) {
                    _gotAddr = relaAddr;
                    std::cout << __FUNCTION__ << " function found in rela.dyn" << std::endl;
                }
            }
        }
    }

    if(pltRela != nullptr)
    {
        for(uint32_t idx = 0; idx < pltRelaSize/sizeof(Elf64_Rela); idx++){
            if(ELF64_R_TYPE(pltRela[idx].r_info) == R_X86_64_JUMP_SLOT) {
                const auto relaAddr = (uint64_t *) (pltRela[idx].r_offset + baseAddr);
                if (*relaAddr == (uint64_t)faddr) {
                    _pltAddr = relaAddr;
                    std::cout << __FUNCTION__ << " function found in rela.plt" << std::endl;
                    break;
                }
            }
        }
    }

    if((_gotAddr == nullptr) && (_pltAddr == nullptr)) {
        std::cerr << __FUNCTION__ << " : failed to find function (" << faddr
                  << ") in .rela.dyn and .rela.plt for " << handle << std::endl;
        std::invalid_argument("Cannot find function in relocation table");
    }
}

template<auto faddr>
bool WrappedFunction<faddr>::wrapping(bool active){
    return _tracer.command([this, active]{
        bool res = true;
        void* addr = active ? _wrapperAddr : (void*)faddr;

        if(_gotAddr != nullptr){
            if (ptrace(PTRACE_POKEDATA, _tracer.getTraceePid(), _gotAddr, addr) == -1) {
                std::cout << "WrappedFunction::wrap : PTRACE_POKEDATA failed : " << strerror(errno) << std::endl;
                res = false;
            }
        }

        if(_pltAddr != nullptr){
            if (ptrace(PTRACE_POKEDATA, _tracer.getTraceePid(), _pltAddr, addr) == -1) {
                std::cout << "WrappedFunction::wrap : PTRACE_POKEDATA failed : " << strerror(errno) << std::endl;
                res = false;
            }
        }
        return res;
    });
}

template<auto faddr>
WrappedFunction<faddr>::~WrappedFunction(){
    wrapping(false);
}

#endif //SPYTESTER_WRAPPEDFUNCTION_H
