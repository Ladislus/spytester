#include <iostream>
#include "../include/SpiedNamespace.h"

SpiedNamespace::SpiedNamespace(bool isBaseNamespace): _id(isBaseNamespace ? LM_ID_BASE : LM_ID_NEWLM)
{}

ElfBin *SpiedNamespace::open(const std::string &binName) {
    void* handle;

    if(_id == LM_ID_NEWLM){
        handle = dlmopen(LM_ID_NEWLM, binName.c_str(), RTLD_LAZY);
    } else {
        handle = dlmopen(_id, binName.c_str(), RTLD_LAZY);
    }

    if(handle == nullptr){
        std::cerr << __FUNCTION__ << " : dlmopen failed : " << dlerror() << std::endl;
        return nullptr;
    }

    if(_id == LM_ID_NEWLM && dlinfo(handle, RTLD_DI_LMID, &_id) == -1) {
        std::cerr << __FUNCTION__ << " : dlinfo RTLD_DI_LMID failed : " << dlerror() << std::endl;
    }

    try{
        ElfBin& bin =  _elfBin.emplace(handle, handle).first->second;
        return &bin;
    } catch(std::invalid_argument& e){
        std::cout << __FUNCTION__ <<" : failed to create ElfBin : " << e.what() << std::endl;
        return nullptr;
    }
}
