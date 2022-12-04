#include <link.h>
#include <iostream>
#include <functional>
#include "../include/DynamicLinker.h"

size_t getDynEntry(struct link_map* lm, Elf64_Sxword tag, std::vector<uint64_t>& entriesFound){

    entriesFound.clear();

    for (Elf64_Dyn* dynEntry = lm->l_ld; dynEntry->d_tag != DT_NULL; dynEntry++)
    {
        if(dynEntry->d_tag == tag){
            entriesFound.push_back(dynEntry->d_un.d_val);
        }
    }

    return entriesFound.size();
}

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

void* getDynSymbolAddrIn(void* addr, Lmid_t lmid){
    Dl_info info;

    if (dladdr(addr, &info) == 0){
        std::cerr << __FUNCTION__ <<" : dladdr failed on " << addr <<" : " << dlerror() << std::endl;
        return nullptr;
    }

    if(info.dli_saddr != addr){
        std::cerr << __FUNCTION__ <<" : " << addr << "is not a valid function pointer " << std::endl;
        return nullptr;
    }

    void* handle = dlmopen(lmid, info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);

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