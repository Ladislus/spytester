#include "../include/SpiedProgram.h"
#include "../include/DynamicLinker.h"

#include <dlfcn.h>
#include <sys/mman.h>

#include <cstring>
#include <iostream>

static const size_t stackSize = 1<<23;

uint32_t SpiedProgram::breakPointCounter = 0;

void defaultOnAddThread(SpiedThread& spiedThread)
{
    std::cout << "New thread "<< spiedThread.getTid() <<" created" <<std::endl;
}

void defaultOnRemoveThread(SpiedThread& spiedThread)
{
    std::cout << "Thread "<< spiedThread.getTid() <<" deleted" <<std::endl;
}

SpiedProgram::SpiedProgram(std::string &&progName, int argc, char *argv, char *envp)
: _progName(progName), _onThreadStart(defaultOnAddThread), _onThreadExit(defaultOnRemoveThread), _lmid(0) {

    _progParam.argc = (uint64_t) argc;
    _progParam.argv = argv;
    _progParam.envp = envp;

    _handle = dlmopen(LM_ID_NEWLM, _progName.c_str(), RTLD_NOW);
    if (_handle == nullptr) {
        std::cerr << __FUNCTION__ << " : dlopen failed for " << _progName << " : " << dlerror() << std::endl;
        throw std::invalid_argument("Invalid program name");
    }

    if (dlinfo(_handle, RTLD_DI_LMID, &_lmid) == -1){
        std::cerr << __FUNCTION__ << " : dlinfo failed RTLD_DI_LMID : " << dlerror() << std::endl;
        throw std::invalid_argument("Failed to get new link map id");
    }

    struct link_map* lm;
    if (dlinfo(_handle, RTLD_DI_LINKMAP, &lm) == -1) {
        std::cerr << __FUNCTION__ << " : dlinfo failed RTLD_DI_LINKMAP : " << dlerror() << std::endl;
        throw std::invalid_argument("Failed to get link map ");
    }

    auto elfHdr = (Elf64_Ehdr*)lm->l_addr;
    Elf64_Addr baseAddr = lm->l_addr;
    _progParam.entryPoint = (void*)(baseAddr + elfHdr->e_entry);

    _stack = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (_stack == MAP_FAILED) {
        std::cerr << __FUNCTION__ << " : stack allocation failed : " << strerror(errno) << std::endl;
        dlclose(_handle);
        throw std::invalid_argument("Cannot allocate stack");
    }

    _tracer = new Tracer(*this);
}

SpiedProgram::~SpiedProgram(){
    _breakPoints.clear();
    _spiedThreads.clear();

    delete _tracer;
    std::cout << __FUNCTION__ << std::endl;
}

void SpiedProgram::start() {
    _tracer->start();
}

void SpiedProgram::resume() {
    for(auto & spiedThread : _spiedThreads)
    {
        spiedThread->resume();
    }
}

void SpiedProgram::stop(){
    for(auto & spiedThread : _spiedThreads)
    {
        spiedThread->stop();
    }
}

void SpiedProgram::terminate() {
    if(!_spiedThreads.empty()){
        _spiedThreads.front()->terminate();
    }
}

ProgParam *SpiedProgram::getProgParam() {
    return &_progParam;
}

const std::string &SpiedProgram::getProgName() {
    return _progName;
}

char *SpiedProgram::getStackTop() {
    return (char*)_stack+stackSize;
}

// Exec Breakpoint Management
BreakPoint *SpiedProgram::createBreakPoint(void *addr, std::string &&name) {
    _breakPoints.emplace_back(std::make_unique<BreakPoint>(*_tracer, std::move(name), addr));
    return _breakPoints.back().get();
}

BreakPoint* SpiedProgram::createBreakPoint(std::string &&symbName) {
    void *addr = dlsym(_handle, symbName.c_str());

    if (addr == nullptr) {
        std::cerr << __FUNCTION__ << " : dlsym failed for "
                  << symbName << " : " << dlerror() << std::endl;
        return nullptr;
    }

    return createBreakPoint(addr, std::move(symbName));
}

BreakPoint *SpiedProgram::getBreakPointAt(void* addr) {
    BreakPoint* bp = nullptr;

    for(auto &breakPoint : _breakPoints){
        if(breakPoint->getAddr() == addr){
            bp = breakPoint.get();
        }
    }

    return bp;
}

// Thread Management
SpiedThread &SpiedProgram::getSpiedThread(pid_t tid) {
    SpiedThread *spiedThread = nullptr;

    // Search thread
    for (auto &ptr: _spiedThreads) {
        if (tid == ptr->getTid()) {
            spiedThread = ptr.get();
            break;
        }
    }

    // Register new thread
    if(spiedThread == nullptr){
        _spiedThreads.emplace_back(std::make_unique<SpiedThread>(*this, *_tracer, tid));

        _callbackMutex.lock();
        _onThreadStart(*_spiedThreads.back());
        _callbackMutex.unlock();

        spiedThread = _spiedThreads.back().get();
    }

    return *spiedThread;
}

void SpiedProgram::setOnThreadStart(std::function<void(SpiedThread &)>&& onThreadStart) {
    std::lock_guard lk(_callbackMutex);

    _onThreadStart = onThreadStart;
}

bool SpiedProgram::relink(std::string &&libName) {
    void* libHandle = dlmopen(_lmid, libName.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    void* testerHandle = dlmopen(LM_ID_BASE, nullptr, RTLD_LAZY | RTLD_NOLOAD) ;

    if((libHandle == nullptr) || (testerHandle == nullptr)) {
        std::cerr << __FUNCTION__ << " : dlmopen failed : "<< dlerror() << std::endl;
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

        std::vector<uint64_t> dynEntries;
        const Elf64_Rela* dynRela = getDynEntry(testerLm, DT_RELA, dynEntries) == 1 ?
                                    (Elf64_Rela*)dynEntries[0] : nullptr;
        const Elf64_Rela* pltRela = getDynEntry(testerLm, DT_JMPREL, dynEntries) == 1 ?
                                    (Elf64_Rela*)dynEntries[0] : nullptr;
        const Elf64_Sym * symTab  = getDynEntry(testerLm, DT_SYMTAB, dynEntries) == 1 ?
                                    (Elf64_Sym*)dynEntries[0] : nullptr;
        const char * strtab       = getDynEntry(testerLm, DT_STRTAB, dynEntries) == 1 ?
                                    (char*)dynEntries[0] : nullptr;
        const size_t dynRelaSize  = getDynEntry(testerLm, DT_RELASZ, dynEntries) == 1 ?
                                    dynEntries[0] : 0U;
        const size_t pltRelaSize  = getDynEntry(testerLm, DT_PLTRELSZ, dynEntries) == 1 ?
                                    dynEntries[0] : 0U;

        bool libNeeded = false;
        getDynEntry(testerLm, DT_NEEDED, dynEntries);

        if((strtab == nullptr) || (symTab == nullptr)) continue;

        for(auto libStrOff : dynEntries){
            if(strcmp(libName.c_str(), strtab+libStrOff) == 0) {
                libNeeded = true;
                std::cout << __FUNCTION__ << " : lib " << libName << " needed for "
                          << testerLm->l_name << std::endl;
                break;
            }
        }

        if(libNeeded)
        {
            auto processRela =
            [this, baseAddr, libHandle, symTab, strtab](const Elf64_Rela* rela){

                const char* symName = (char*)strtab + symTab[ELF64_R_SYM(rela->r_info)].st_name;
                void * symAddr= getDefinition(libHandle, symName);

                if( symAddr != nullptr){
                    if ((ELF64_R_TYPE(rela->r_info) == R_X86_64_GLOB_DAT) ||
                        (ELF64_R_TYPE(rela->r_info) == R_X86_64_JUMP_SLOT))
                    {
                        auto relaAddr = (uint64_t *) (rela->r_offset + baseAddr);
                        if (_tracer->commandPTrace(true, PTRACE_POKEDATA, _tracer->getTraceePid(),
                                                   relaAddr, symAddr) == -1)
                        {
                            std::cerr << "SpiedProgram::relink : PTRACE_POKEDATA failed : "
                                      << strerror(errno) << std::endl;
                        }

                        std::cout << "SpiedProgram::relink : " << symName << " relinked" << std::endl;
                    } else {
                        std::cerr << "SpiedProgram::relink : unexpected relocation type ("
                                  << ELF64_R_TYPE(rela->r_info) << ") for " << symName << std::endl;
                    }
                }
            };

            if(dynRela != nullptr){
                for(uint32_t idx = 0; idx < dynRelaSize/sizeof(Elf64_Rela); idx++){
                    processRela(&dynRela[idx]);
                }
            }

            if(pltRela) {
                for (uint32_t idx = 0; idx < pltRelaSize / sizeof(Elf64_Rela); idx++) {
                    processRela(&pltRela[idx]);
                }
            }
        }

        testerLm = testerLm->l_next;
    }

    return true;
}




