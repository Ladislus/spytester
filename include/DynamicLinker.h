#ifndef SPYTESTER_DYNAMICLINKER_H
#define SPYTESTER_DYNAMICLINKER_H

#include <elf.h>
#include <vector>
#include <dlfcn.h>
#include <thread>
#include <future>
#include "Tracer.h"

class DynamicLinker {
public:
    DynamicLinker(int argc, const char* argv[], char* envp[] );
    ~DynamicLinker();
    DynamicLinker(const DynamicLinker&) = delete;

    void preload();
    bool relink(const std::string &libName, Tracer &tracer) const;

    void* convertDynSymbolAddr(void* addr) const;
    void* getRelaAddr(void* addr,const std::string& binName) const;

private:
    using DynVector = std::vector<std::vector<Elf64_Dyn*>>;
    static DynVector getDynEntries(struct link_map* lm);

    Lmid_t _spiedNamespace;
    void* _loaderHandle;
    void* _executableHandle;

    std::future<void*> _futureHandle;

    std::thread loader;

    int _argc;
    const char** _argv;
    char** _envp;
};


size_t getDynEntry(struct link_map* lm, Elf64_Sxword tag, std::vector<uint64_t>& entriesFound);
void* getDefinition(void* handle, const char* symName);

#endif //SPYTESTER_DYNAMICLINKER_H