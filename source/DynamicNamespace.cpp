#include <csignal>
#include <iostream>
#include <thread>

#include "DynamicNamespace.h"
#include "SpyLoader.h"
#include "Logger.h"

static struct link_map* getLinkMap(Lmid_t id){
    void* handle = dlmopen(id, "libSpyLoader.so", RTLD_LAZY);
    if(handle == nullptr) {
        throw (std::invalid_argument(std::string(__FUNCTION__) + " : failed to find libSpyLoader.so in namespace " +
                                     std::to_string(id) + " : " + dlerror()));
    }

    struct link_map* lm;
    if(dlinfo(handle, RTLD_DI_LINKMAP, &lm) != 0){
        throw (std::invalid_argument(std::string(__FUNCTION__) + " : failed to get link map in namespace " +
                                     std::to_string(id) + " : " + dlerror()));
    }

    while(lm->l_prev) lm = lm->l_prev;

    return lm;
}

DynamicNamespace::DynamicNamespace(): 
    _id(LM_ID_BASE),
    _lm(getLinkMap(_id)),
    _argc(0),
    _argv(nullptr),
    _envp(nullptr),
    _executable(),
    _loader("libSpyLoader.so", _id),
    _createMainThread(nullptr) {}


DynamicNamespace::DynamicNamespace(int argc, const char* argv[], char **envp):
    _id(getSpyLoader().reserveNamespaceId(*this)),
    _lm(getLinkMap(_id)),
    _argc(argc),
    _argv(argv),
    _envp(envp),
    _executable(),
    _loader("libSpyLoader.so", _id), // IMPROVE if loader constructor throw exception, we won't be able to release _id
    _createMainThread((decltype(_createMainThread)) _loader.getSymbol((void*) &DynamicNamespace::createMainThread)) {
    if(_createMainThread == nullptr) {
        getSpyLoader().releaseNamespaceId(_id);
        throw (std::invalid_argument(std::string(__FUNCTION__) +
                                     " : failed to find createMainThread in the spied dynamic namespace " +
                                     std::to_string(_id)));
    }
}

DynamicNamespace::~DynamicNamespace() {
    if(_id != LM_ID_BASE)
        getSpyLoader().releaseNamespaceId(_id);
}

DynamicModule *DynamicNamespace::load(const std::string &binName) {
    try{
        DynamicModule& bin =  _dynamicLib.emplace(std::piecewise_construct,
                                                  std::make_tuple(binName),
                                                  std::make_tuple(binName, _id)).first->second;
        return &bin;
    } catch(std::invalid_argument& e){
        error_log("Failed to create DynamicModule (" << e.what() << ")");
        return nullptr;
    }
}

void DynamicNamespace::unload(const std::string& binName){
    this->_dynamicLib.erase(binName);
}

void DynamicNamespace::createMainThread(DynamicNamespace *ns) {
    if(ns->_createMainThread == &DynamicNamespace::createMainThread) {
        // Create main thread using the current namesapce libpthread
        auto mainThread = std::thread(&DynamicNamespace::loadExecutable, ns);
        mainThread.detach();

        raise(SIGSTOP);
    } else {
        // Jump to DynamicNamespace::createMainThread defined in the spied namespace
        ns->_createMainThread(ns);
    }
}

void start(void* entryPoint, int argc, const char* argv[], char* envp[]);

void DynamicNamespace::loadExecutable() {
    DynamicModule& exec = _executable.emplace(_argv[0], _id);

    void* entryPoint = exec.getEntryPoint();
    if(entryPoint == nullptr){
        error_log("Cannot find entry point in " << _argv[0]);
    }

    // Give user some time after loading to do some actions before entering the main
    raise(SIGSTOP);

    start(entryPoint, _argc, _argv, _envp);
}

void *DynamicNamespace::convertDynSymbolAddr(void *addr) const {
    Dl_info info;

    if (dladdr(addr, &info) == 0){
        error_log("Dladdr failed on " << addr << " (" << dlerror() << ")");
        return nullptr;
    }

    if(info.dli_saddr != addr){
        error_log(addr << "is not a valid function pointer");
        return nullptr;
    }

    void* handle = dlmopen(_id, info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);

    if(handle == nullptr){
        error_log("Dlmopen failed (" << dlerror() << ")");
        return nullptr;
    }

    void* retAddr = dlsym(handle, info.dli_sname);
    if(!retAddr)
        error_log("Dlsym failed (" << dlerror() << ")");

    return retAddr;
}

bool DynamicNamespace::isContaining(struct link_map *lm) const {
    auto lmIt = _lm;

    while(lmIt != nullptr){
        if(lmIt == lm) return true;
        lmIt = lmIt->l_next;
    }

    return false;
}

bool DynamicNamespace::iterateOverModule(const std::function<bool(DynamicModule &)> &f) {
    bool res(false);
    syncModules();

    for(auto& dynModule : _dynamicLib){
        res = f(dynModule.second);
        if(!res) break;
    }

    if(res && _executable.has_value()){
        res = f(_executable.value());
    }

    return res;
}

void DynamicNamespace::syncModules() {
    auto lmIt = _lm;

    while(lmIt != nullptr){
        auto it = _dynamicLib.find(lmIt->l_name);

        if(it == _dynamicLib.cend()){
            if(lmIt->l_name[0] == '\0' && !_executable.has_value()){
                _executable.emplace("", _id);
            } else if(lmIt->l_name[0] == '/') {
                std::string path(lmIt->l_name);
                size_t lastSlash = path.find_last_of('/');

                _dynamicLib.emplace(std::piecewise_construct,
                                    std::make_tuple(path.substr(lastSlash+1)),
                                    std::make_tuple(path.substr(lastSlash+1), _id));
            }
        }

        lmIt = lmIt->l_next;
    }
}

// #FIXME not safe and clean : maybe start should be rewritten in assembly
void start(void* entryPoint, int argc, const char* argv[], char* envp[]){
    uint64_t* sp;
    __asm__ volatile (" movq %%rsp, %0" : "=r"(sp)::);

    char** env = envp;
    for(; *env != nullptr; env++); // set env point to the last environment variable

    uint64_t wordsToPush = static_cast<uint64_t>(argc + 4 + (env-envp));

    // guarantee that the stack pointer will be 16-bytes aligned when entryPoint is called
    if((uint64_t)(sp + wordsToPush) & 0xF){
        sp--;
        *sp = 0LU;
    }

    // Push empty auxiliary vector onto the stack
    sp--;
    *sp = 0LU;

    // Push environment pointers onto the stack
    for(; env >= envp; env--){
        sp--;
        *sp = (uint64_t)*env;
    }

    // Push argument pointers onto the stack
    sp--;
    *sp = 0LU;

    for(int i = argc - 1; i>=0; i--){
        sp--;
        *sp = (uint64_t)argv[i];
    }

    // Push argc onto the stack
    sp--;
    *sp = static_cast<uint64_t>(argc);

    //initialize register and jump to entry point
    __asm__ volatile(
            "xor %%rbp, %%rbp \n"
            "xor %%rdx, %%rdx \n"
            "movq %0, %%rsp \n"
            "jmp *%1 \n"
            ::"r"(sp), "r"(entryPoint): "%rdx");

    // dummy call to force gcc to decrement sp at start entry
    std::exit(0);
}


