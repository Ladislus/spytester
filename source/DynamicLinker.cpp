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