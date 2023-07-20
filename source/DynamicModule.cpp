#include <iostream>
#include <stdexcept>

#include "DynamicModule.h"

// constructor helpers
static void* openHandle(const std::string & name, Lmid_t id){
    void* handle = dlmopen(id, name.c_str(), RTLD_LAZY);
    if(handle == nullptr){
        throw(std::invalid_argument(std::string(__FUNCTION__) + " : failed to dlmopen "+name+ " : "+ dlerror()));
    }

    return handle;
}

static struct link_map* getLinkMap(void* handle){
    struct link_map* lm;
    if(dlinfo(handle, RTLD_DI_LINKMAP, &lm) == -1){
        throw(std::invalid_argument(std::string(__FUNCTION__) + " : failed to get link map "+ dlerror()));
    }

    return lm;
}

DynamicModule::DynamicModule(const std::string &name, Lmid_t id)
: _name(name), _handle(openHandle(name, id)), _lm(getLinkMap(_handle)), _elf(ElfFile::getElfFile(_lm->l_name))
{}

void *DynamicModule::getDynamicSymbol(const std::string &symbName) const {
    void* symbAddr = nullptr;
    auto& dynstr = _elf.getDynStrTab();

    for(auto& symb : _elf.getDynSymTab()){
        uint8_t type = ELF64_ST_TYPE(symb.st_info);

        if ((type == STT_FUNC || type == STT_OBJECT) &&     // symbol is a function or an object
            (symb.st_shndx != 0) &&                         // symbol is defined in the binary
            symbName == &dynstr[symb.st_name])              // symbol name match symbName
        {
            symbAddr = (void *) (_lm->l_addr + symb.st_value);
            break;
        }
    }

    return symbAddr;
}

void *DynamicModule::getSymbol(const std::string &symbName) const {
    void* symbolAddr = getDynamicSymbol(symbName);

    // if failed to find the symbol in dynamic symbol
    if(symbolAddr == nullptr) {
        auto& strtab = _elf.getStrTab();

        for (auto& symb : _elf.getSymTab()) {
            uint8_t type = ELF64_ST_TYPE(symb.st_info);

            if ((type == STT_FUNC || type == STT_OBJECT) &&     // symbol is a function or an object
                (symb.st_shndx != 0) &&                         // symbol is defined in the binary
                symbName == &strtab[symb.st_name])              // symbol name match symbName
            {
                symbolAddr = (void *) (_lm->l_addr + symb.st_value);
                break;
            }
        }
    }

    return symbolAddr;
}

std::string DynamicModule::getMangledName(void *symbolPtr) {
    std::string mangledName;
    Dl_info info;

    dladdr(symbolPtr, &info);
    if(info.dli_sname != nullptr){
        mangledName = info.dli_sname;
    }

    return mangledName;
}

void *DynamicModule::getSymbol(void *symbolPtr) const{
    void* symb = nullptr;

    std::string mangledName = getMangledName(symbolPtr);
    if(!mangledName.empty()){
        symb = getSymbol(mangledName);
    }

    return symb;
}

DynamicModule::~DynamicModule() {
    for(auto relinkage : _inRelinkages){
        relinkage->invalidate();
    }

    dlclose(_handle);
}

void DynamicModule::relink(DynamicModule &module) {

    auto& dynstr = _elf.getDynStrTab();

    for(auto& dyn: _elf.getDynamic()){
        if(dyn.d_tag == DT_NEEDED && &dynstr[dyn.d_un.d_val] == module.getName()){
            // #FIXME add try catch if relink failed
            _outRelinkages.erase(module.getName());
            _outRelinkages.emplace(std::piecewise_construct,
                                   std::make_tuple(module.getName()),
                                   std::tuple<DynamicModule &, DynamicModule &>(*this, module));
            break;
        }
    }
}

void DynamicModule::unrelink(const std::string& libName) {
    _outRelinkages.erase(libName);
}

void DynamicModule::addInRelinkage(Relinkage &relinkage) {
    _inRelinkages.insert(&relinkage);
}

void DynamicModule::removeInRelinkage(Relinkage &relinkage) {
    _inRelinkages.erase(&relinkage);
}

const std::string &DynamicModule::getName() const {
    return _name;
}

void *DynamicModule::getEntryPoint() const {
    Elf64_Addr off = _elf.getEntryPoint();

    // If the entry point offset is clearly not significant return nullptr instead of module base address
    if(off == 0){
        return nullptr;
    }

    return (void*)(_lm->l_addr + off);
}

void DynamicModule::iterateOverRelocations(const std::function<bool(uint32_t, const std::string &, uint64_t *)>& f) {
    const auto& dynstr = _elf.getDynStrTab();
    const auto& dynsym = _elf.getDynSymTab();

    for(const auto& rela: _elf.getRela()){
        auto relaAddr =  (uint64_t*) (_lm->l_addr + rela.r_offset);
        uint32_t relaType = ELF64_R_TYPE(rela.r_info);
        const char* name = &dynstr[dynsym[ELF64_R_SYM(rela.r_info)].st_name];

        if(!f(relaType, name, relaAddr)) break;
    }
}





