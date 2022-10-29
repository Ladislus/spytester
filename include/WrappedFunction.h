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

template<typename TRET, typename ... TARGS>
class WrappedFunction {

public:
    WrappedFunction(Tracer& tracer, std::string& binName, TRET (*function)(TARGS ...));
    ~WrappedFunction();

    void set(TRET (*wrapper)(TARGS ...));
    void reset();
    using FctType = TRET(*)(TARGS ...);

private:
    Tracer& _tracer;
    uint64_t* _gotAddr;
    FctType _function;
    FctType _wrapper;
    void* _handle;
};

template<typename TRET, typename... TARGS>
void WrappedFunction<TRET, TARGS...>::set(TRET (*wrapper)(TARGS...)) {
    if(_wrapper != wrapper) {
        if (_tracer.isTracerThread()) {
            if (ptrace(PTRACE_POKEDATA, _tracer.getTraceePid(), _gotAddr, wrapper) == -1) {
                std::cout << __FUNCTION__ << " : PTRACE_POKEDATA failed : " << strerror(errno) << std::endl;
            } else {
                _wrapper = wrapper;
            }
        }
        else {
            _tracer.command(Tracer::make_unique_cmd([this, wrapper]{set(wrapper);}));
        }
    }
}

template<typename TRET, typename... TARGS>
void WrappedFunction<TRET, TARGS...>::reset() {
    set(_function);
}

template<typename TRET, typename... TARGS>
WrappedFunction<TRET, TARGS...>::~WrappedFunction() {
    reset();
}

template<typename TRET, typename... TARGS>
WrappedFunction<TRET, TARGS...>::WrappedFunction(Tracer& tracer, std::string &binName, TRET (*function)(TARGS...))
:_tracer(tracer), _function(function), _wrapper(function), _gotAddr(nullptr)
{
    // .got.plt entry corresponding to wrapped function must be bind in binName to be identified
    _handle = dlopen(binName.c_str(), RTLD_NOLOAD | RTLD_NOW);
    if(_handle == nullptr)
    {
        std::cerr << __FUNCTION__ << " : dlopen failed for " << binName << " : " << dlerror() << std::endl;
        throw std::invalid_argument("Invalid binary name");
    }

    struct link_map* lm;
    if(dlinfo(_handle, RTLD_DI_LINKMAP, &lm) == -1)
    {
        std::cerr << __FUNCTION__ << " : dlinfo failed for " << binName << " : " << dlerror() << std::endl;
        throw std::invalid_argument("Cannot get dynamic linker info");
    }

    const Elf64_Addr baseAddr = lm->l_addr;
    Elf64_Dyn* dynEntry = lm->l_ld;

    const Elf64_Rela* dynRela = nullptr;
    const Elf64_Rela* pltRela = nullptr;
    size_t dynRelaSize = 0;
    size_t pltRelaSize = 0;

    for (dynEntry = lm->l_ld; dynEntry->d_tag != DT_NULL; dynEntry++)
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
        for(uint32_t idx = 0; idx < dynRelaSize/sizeof(Elf64_Rela); idx++)
        {
            if(ELF64_R_TYPE(dynRela[idx].r_info) == R_X86_64_GLOB_DAT) {
                uint64_t* relaAddr = (uint64_t *) (dynRela[idx].r_offset + baseAddr);
                if (*relaAddr == (uint64_t)_function) {
                    _gotAddr = relaAddr;
                    std::cout << __FUNCTION__ << " function found in rela.dyn" << std::endl;
                    break;
                }
            }
        }
    }

    if((_gotAddr == nullptr) && (pltRela != nullptr))
    {
        for(uint32_t idx = 0; idx < pltRelaSize/sizeof(Elf64_Rela); idx++)
        {
            if(ELF64_R_TYPE(pltRela[idx].r_info) == R_X86_64_JUMP_SLOT) {
                const auto relaAddr = (uint64_t *) (pltRela[idx].r_offset + baseAddr);
                if (*relaAddr == (uint64_t)_function) {
                    _gotAddr = relaAddr;
                    std::cout << __FUNCTION__ << " function found in rela.plt" << std::endl;
                    break;
                }
            }
        }
    }

    if(_gotAddr == nullptr)
    {
        std::cerr << __FUNCTION__ << " : failed to find function (" << _function << ") in .rela.dyn and .rela.plt"
                  << std::endl;
        std::invalid_argument("Cannot find function in relocation table");
    }
}


#endif //SPYTESTER_WRAPPEDFUNCTION_H
