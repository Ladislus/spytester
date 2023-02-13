#include <link.h>
#include <iostream>
#include <functional>
#include <thread>
#include "../include/DynamicLinker.h"

#define DYN_VECTOR_SIZE 35

void* getDefinition(void* handle, const char* symName){
    struct link_map* lm;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) == -1){
        std::cerr << "dlinfo failed : " << dlerror() << std::endl;
        return nullptr;
    }

    Dl_info info;
    void* sym = dlsym(handle, symName);

    if ((sym == nullptr) ||
        (dladdr(sym, &info) == 0) ||
        (info.dli_fbase != (void*)lm->l_addr))
    {
        sym = nullptr;
    }

    return sym;
}

void* DynamicLinker::convertDynSymbolAddr(void* addr) const{
    Dl_info info;

    if (dladdr(addr, &info) == 0){
        std::cerr << __FUNCTION__ <<" : dladdr failed on " << addr <<" : " << dlerror() << std::endl;
        return nullptr;
    }

    if(info.dli_saddr != addr){
        std::cerr << __FUNCTION__ <<" : " << addr << "is not a valid function pointer " << std::endl;
        return nullptr;
    }

    void* handle = dlmopen(_spiedNamespace, info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);

    if(handle == nullptr){
        std::cerr << __FUNCTION__ <<" : dlmopen failed : " << dlerror() << std::endl;
        return nullptr;
    }

    void* retAddr = dlsym(handle, info.dli_sname);
    if(retAddr == nullptr){
        std::cerr << __FUNCTION__ <<" : dlsym failed : "<<dlerror() << std::endl;
    }

    return retAddr;
}

DynamicLinker::DynamicLinker(int argc, const char * argv[], char **envp):
_argc(argc), _argv(argv), _envp(envp), _executableHandle(nullptr), _spiedNamespace(0)
{
    _loaderHandle = dlmopen(LM_ID_NEWLM, "libSpyLoader.so", RTLD_NOW);
    if (_loaderHandle == nullptr) {
        std::cerr << __FUNCTION__ << " : dlopen failed for libSpiedLoader.so : " << dlerror() << std::endl;
        throw std::invalid_argument("Invalid program name");
    }

    if (dlinfo(_loaderHandle, RTLD_DI_LMID, &_spiedNamespace) == -1){
        std::cerr << __FUNCTION__ << " : dlinfo failed RTLD_DI_LMID : " << dlerror() << std::endl;
        throw std::invalid_argument("Failed to get new link map id");
    }
}

void DynamicLinker::preload() {
    if(_executableHandle != nullptr){
        std::cerr << __FUNCTION__ << " : " << _argv[0] <<" is already loaded" << std::endl;
        return;
    }

    auto load = reinterpret_cast<void(*)(int, const char**, char**, std::promise<void*>)>
            (dlsym(_loaderHandle, "preload"));
    if(load == nullptr){
        std::cerr << __FUNCTION__ << " : dlsym failed for load : " << dlerror() << std::endl;
        return;
    }

    std::promise<void*> p;
    _futureHandle = p.get_future();

    load(_argc, _argv, _envp, std::move(p));
}

bool DynamicLinker::relink(const std::string &libName, Tracer &tracer) const {

    void* libHandle = dlmopen(_spiedNamespace, libName.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    if(libHandle == nullptr) {
        std::cerr << __FUNCTION__ << " : dlmopen failed for "<< libName <<" : " << dlerror() << std::endl;
        return false;
    }

    void* testerHandle = dlmopen(LM_ID_BASE, nullptr, RTLD_LAZY | RTLD_NOLOAD) ;
    if(testerHandle == nullptr){
        std::cerr << __FUNCTION__ << " : dlmopen failed for testerHandle : "<< dlerror() << std::endl;
        return false;
    }

    struct link_map* testerLm;
    if (dlinfo(testerHandle, RTLD_DI_LINKMAP, &testerLm) == -1){
        std::cerr << "dlinfo failed : " << dlerror() << std::endl;
        return false;
    }

    // Get the first object tester link map
    while(testerLm->l_prev != nullptr){
        testerLm = testerLm->l_prev;
    }

    while(testerLm != nullptr){
        const Elf64_Addr baseAddr = testerLm->l_addr;

        DynVector dynEntries = getDynEntries(testerLm);

        if((dynEntries[DT_STRTAB].empty()) || (dynEntries[DT_SYMTAB].empty())) continue;

        auto symTab  = (const Elf64_Sym *)dynEntries[DT_SYMTAB][0]->d_un.d_ptr;
        auto strtab  = (const char *)dynEntries[DT_STRTAB][0]->d_un.d_ptr;

        bool libNeeded = false;

        for(auto libStrOff : dynEntries[DT_NEEDED]){
            if(strcmp(libName.c_str(), strtab+libStrOff->d_un.d_val) == 0) {
                libNeeded = true;
                std::cout << __FUNCTION__ << " : lib " << libName << " needed for "
                          << testerLm->l_name << std::endl;
                break;
            }
        }

        if(libNeeded)
        {
            auto processRela =
                [baseAddr, libHandle, symTab, strtab, &tracer](const Elf64_Rela* rela){

                    const char* symName = (char*)strtab + symTab[ELF64_R_SYM(rela->r_info)].st_name;
                    void * symAddr= getDefinition(libHandle, symName);

                    if( symAddr != nullptr){
                        if ((ELF64_R_TYPE(rela->r_info) == R_X86_64_GLOB_DAT) ||
                            (ELF64_R_TYPE(rela->r_info) == R_X86_64_JUMP_SLOT))
                        {
                            auto relaAddr = (uint64_t *) (rela->r_offset + baseAddr);
                            tracer.writeWord(relaAddr, (uint64_t)symAddr);
                            std::cout << "SpiedProgram::relink : " << symName << " relinked" << std::endl;
                        } else {
                            std::cerr << "SpiedProgram::relink : unexpected relocation type ("
                                      << ELF64_R_TYPE(rela->r_info) << ") for " << symName << std::endl;
                        }
                    }
                };

            if(!dynEntries[DT_RELA].empty() && !dynEntries[DT_RELASZ].empty()){

                auto dynRela = (const Elf64_Rela*)dynEntries[DT_RELA][0]->d_un.d_ptr;
                const size_t relaSize = dynEntries[DT_RELASZ][0]->d_un.d_val;

                for(uint32_t idx = 0; idx < relaSize/sizeof(Elf64_Rela); idx++){
                    processRela(&dynRela[idx]);
                }
            }

            if(!dynEntries[DT_JMPREL].empty() && !dynEntries[DT_PLTRELSZ].empty()){

                auto pltRela = (const Elf64_Rela*)dynEntries[DT_JMPREL][0]->d_un.d_ptr;
                const size_t relaSize = dynEntries[DT_PLTRELSZ][0]->d_un.d_val;

                for(uint32_t idx = 0; idx < relaSize/sizeof(Elf64_Rela); idx++){
                    processRela(&pltRela[idx]);
                }
            }
        }

        testerLm = testerLm->l_next;
    }

    return true;
}

void *DynamicLinker::getRelaAddr(void *defAddr, const std::string &binName) const {
    void* res = nullptr;

    void* handle = dlmopen(_spiedNamespace, binName.c_str(), RTLD_NOLOAD | RTLD_NOW);
    if(handle == nullptr){
        std::cerr << __FUNCTION__ << " : dlmopen failed for " << binName << " : " << dlerror() << std::endl;
        return nullptr;
    }

    struct link_map* lm;
    if(dlinfo(handle, RTLD_DI_LINKMAP, &lm) == -1){
        std::cerr << __FUNCTION__ << " : dlinfo failed for "<< binName << " : " << dlerror() << std::endl;
        return res;
    }

    const Elf64_Addr baseAddr = lm->l_addr;

    DynVector dynEntries = getDynEntries(lm);
    const Elf64_Rela* dynRela = dynEntries[DT_RELA].empty() ? nullptr
            : (Elf64_Rela*)dynEntries[DT_RELA][0]->d_un.d_ptr;
    const Elf64_Rela* pltRela = dynEntries[DT_JMPREL].empty() ? nullptr
            : (Elf64_Rela*)dynEntries[DT_JMPREL][0]->d_un.d_ptr;
    const size_t dynRelaSize  = dynEntries[DT_RELASZ].empty() ? 0U
            : dynEntries[DT_RELASZ][0]->d_un.d_val;
    const size_t pltRelaSize  = dynEntries[DT_PLTRELSZ].empty() ? 0U
            : dynEntries[DT_PLTRELSZ][0]->d_un.d_val;

    auto processRelaTable = [&res, defAddr, baseAddr] (const Elf64_Rela relaTable[], const size_t size){
        if(relaTable != nullptr){
            for(uint32_t idx = 0; idx < size/sizeof(Elf64_Rela); idx++){
                if ((ELF64_R_TYPE(relaTable[idx].r_info) == R_X86_64_GLOB_DAT) ||
                    (ELF64_R_TYPE(relaTable[idx].r_info) == R_X86_64_JUMP_SLOT))
                {
                    const auto relaAddr = (uint64_t *) (relaTable[idx].r_offset + baseAddr);
                    if (*relaAddr == (uint64_t)defAddr) {
                        res = relaAddr;
                    }
                }
            }
        }
    };

    processRelaTable(dynRela, dynRelaSize);
    processRelaTable(pltRela, pltRelaSize);

    return res;
}

DynamicLinker::DynVector DynamicLinker::getDynEntries(struct link_map *lm) {
    DynVector dynEntries(DYN_VECTOR_SIZE);

    for (Elf64_Dyn* dynEntry = lm->l_ld; dynEntry->d_tag != DT_NULL; dynEntry++)
    {
        if(dynEntry->d_tag < DYN_VECTOR_SIZE){
            dynEntries[dynEntry->d_tag].push_back(dynEntry);
        }
    }

    return dynEntries;
}

DynamicLinker::~DynamicLinker() {
    std::cout << __FUNCTION__ << std::endl;
    // #FIXME close handles
}

