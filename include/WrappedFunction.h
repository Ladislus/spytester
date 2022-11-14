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
#include "DynamicLinker.h"

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
    void* _relaAddr;

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
_tracer(tracer), _wrapperAddr((void*)getWrapperFctPtr(faddr)), _relaAddr(nullptr),
_handle(handle){
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

    auto processRelaTable = [this, baseAddr]
    (const Elf64_Rela relaTable[], const size_t size){
        if(relaTable != nullptr)
        {
            for(uint32_t idx = 0; idx < size/sizeof(Elf64_Rela); idx++){
                if ((ELF64_R_TYPE(relaTable[idx].r_info) == R_X86_64_GLOB_DAT) ||
                    (ELF64_R_TYPE(relaTable[idx].r_info) == R_X86_64_JUMP_SLOT))
                {
                    const auto relaAddr = (uint64_t *) (relaTable[idx].r_offset + baseAddr);
                    if (*relaAddr == (uint64_t)faddr) {
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
    return _tracer.command([this, active]{
        bool res = true;
        void* addr = active ? _wrapperAddr : (void*)faddr;

        if(_relaAddr != nullptr){
            if (ptrace(PTRACE_POKEDATA, _tracer.getTraceePid(), _relaAddr, addr) == -1) {
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
