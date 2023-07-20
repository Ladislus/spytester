#include <algorithm>
#include <cstring>
#include <iostream>
#include <list>

#include "SpyLoader.h"
#include "Logger.h"

#include "helpers/filesystem.h"

// Can be overload using SPIED_NAMESPACES_NB environnement variable
#define SPIED_NAMESPACES_NB 3

struct list_head
{
    struct list_head *next;
    struct list_head *prev;
}; // from glibc/include/list_t.h : pretty dirty but list_t is not likely to frequently change

// lib ld static tls initialization of existing thread workaround
extern void* _rtld_global;

// libc overload method
extern "C" {
    int pthread_key_create(pthread_key_t *key, void (*destructor)(void*)) noexcept;
    int pthread_key_delete(pthread_key_t key) noexcept;
    void __ctype_init() noexcept;
    void dl_init_static_tls(struct link_map* lm) noexcept;
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) noexcept {
    auto& loader = getSpyLoader();
    return loader.pthreadKeyCreate(key, destructor);
}

int pthread_key_delete(pthread_key_t key) noexcept {
    auto& loader = getSpyLoader();
    return loader.pthreadKeyDelete(key);
}

void __ctype_init() noexcept {
    auto& loader = getSpyLoader();
    return loader.ctypeInit();
}

void dl_init_static_tls(struct link_map* lm) noexcept {
    auto& loader = getSpyLoader();
    loader.initStaticTLS(lm);
}

SpyLoader::SpyLoaderInitializer SpyLoader::spyLoaderInitializer;

SpyLoader::SpyLoaderInitializer::SpyLoaderInitializer() {;
    Lmid_t lmid;

    void* handle = dlopen("libc.so.6", RTLD_LAZY | RTLD_NOLOAD);
    dlinfo(handle,  RTLD_DI_LMID, &lmid);

    if(lmid == LM_ID_BASE){
        int namespaceNb = SPIED_NAMESPACES_NB;
        const char* env = getenv("SPIED_NAMESPACES_NB");

        if(env != nullptr){
            int n = atoi(env);
            if(n > 0 && n < 11) namespaceNb = n;
        }

        SpyLoader::spyLoader = new SpyLoader();
        SpyLoader::spyLoader->createSpiedNamespaces(static_cast<uint32_t>(namespaceNb));
    } else {
        handle = dlmopen(LM_ID_BASE, "libSpyLoader.so", RTLD_LAZY | RTLD_NOLOAD);
        if(!handle)
            fatal_log("Dlmopen libSpyLoader.so failed : " << dlerror());

        auto getMasterSpyLoader =  dlsym(handle, "getSpyLoader");
        SpyLoader::spyLoader = &((SpyLoader& (*)())getMasterSpyLoader)();
    }

    info_log("SpyLoader in namespace " << lmid);
}

SpyLoader &getSpyLoader() {
    return *SpyLoader::spyLoader;
}

SpyLoader* SpyLoader::spyLoader;

SpyLoader::SpyLoader() : _baseNamespace() {

    // load libpthread in the current namespace
    DynamicModule* libpthread = _baseNamespace.load("libpthread.so.0");
    if(!libpthread)
        fatal_log("Failed to load libpthread.so.0 in base namespace");

    void* pthread_init_static_tls = libpthread->getSymbol("__pthread_init_static_tls");
    if(!pthread_init_static_tls)
        fatal_log("Failed to find __pthread_init_static_tls in libpthread.so.0 in base namespace");

    auto rtld_global_it = (uint64_t*)&_rtld_global;
    // #FIXME check we are not going past the end of _rtld_global
    while(*rtld_global_it != (uint64_t)pthread_init_static_tls){
        rtld_global_it++;
    }
    _dlInitStaticTLS = (init_static_tls_fptr*) rtld_global_it;

    // get __stack_user which is used by libpthread to keep track of threads managed by the library
    _baseStackUserList = (list_t*) libpthread->getSymbol("__stack_user");
    if(!this->_baseStackUserList)
        fatal_log("Failed to find __stack_user in libpthread.so.0 in base namespace");

    _defaultThreadStack = _baseStackUserList->next;

    updateWrappedFunctions(_baseNamespace);
}

void SpyLoader::createSpiedNamespaces(uint32_t nb) {

    if(!_avlNamespaceId.empty() || !_usedNamespace.empty() ){
        error_log("Should only be called once");
        return;
    }

    std::list<DynamicNamespace> namespaces;

    // 'canonical("/proc/self/exe")' is a UNIX specific way to obtain current executable path (not current working directory)
    const Path libSpyLoaderPath = fs::canonical("/proc/self/exe").parent_path().append("libSpyLoader.so");

    for(uint32_t i = 0; i < nb; i++){
        void* handle = dlmopen(LM_ID_NEWLM, libSpyLoaderPath.c_str(), RTLD_LAZY);

        if(handle == nullptr){
            error_log("Failed to load libSpyLoader.so " << dlerror());
            continue;
        }

        Lmid_t id;
        if(dlinfo(handle, RTLD_DI_LMID, &id) != 0){
            error_log("Dlinfo failed : " << dlerror());
            continue;
        }

        _avlNamespaceId.insert(id);
        updateWrappedFunctions(namespaces.emplace_back(0, nullptr, nullptr));

        // No need to try to close libSpyLoader.so which is tagged NODELETE so whatever happen it should stay in loaded
    }
}

void SpyLoader::updateWrappedFunctions(DynamicNamespace &spiedNamespace) {
    // libpthread
    DynamicModule* libpthread = spiedNamespace.load("libpthread.so.0");
    if(!libpthread)
        fatal_log("Failed to load libpthread.so.0");

    // init_static_tls
    void* pthread_init_static_tls = libpthread->getSymbol("__pthread_init_static_tls");
    if(!pthread_init_static_tls)
        fatal_log("Failed to find __pthread_init_static_tls in libpthread.so.0");

    _init_static_tls_functions.push_back((init_static_tls_fptr) pthread_init_static_tls);

    // override _rtld_global._dl_init_static_tls with our custom function
    *_dlInitStaticTLS = &dl_init_static_tls;

    // pthread_key_delete
    auto pthread_key_del = libpthread->getSymbol("pthread_key_delete");
    if(!pthread_key_del)
        fatal_log("Failed to find pthread_key_delete in libpthread.so.0");

    _pthread_key_delete_functions.push_back( (pthread_key_delete_fptr) pthread_key_del);

    // pthread_key_create
    auto pthread_key_cre = libpthread->getSymbol("pthread_key_create");
    if(!pthread_key_cre)
        fatal_log("Failed to find pthread_key_create in libpthread.so.0");

    _pthread_key_create_functions.push_back((pthread_key_create_fptr) pthread_key_cre);

    // repair __stack_user
    auto stackUserList = (list_t*) libpthread->getSymbol("__stack_user");
    if(!stackUserList)
        fatal_log("Failed to find __stack_user in libpthread.so.0");

    // remove default thread from the list managed by libpthread
    stackUserList->prev = stackUserList;
    stackUserList->next = stackUserList;

    // add default thread in the list managed by libpthread in base namespace
    _defaultThreadStack->next = _baseStackUserList;
    _defaultThreadStack->prev = _baseStackUserList;
    _baseStackUserList->next = _defaultThreadStack;
    _baseStackUserList->prev = _defaultThreadStack;

    // libc
    DynamicModule* libc = spiedNamespace.load("libc.so.6");
    if(!libc)
        fatal_log("Failed to load libc.so.6");

    // __ctype_init
    void* ctype_init = libc->getSymbol("__ctype_init");
    if(!ctype_init)
        fatal_log("Failed to find __ctype_init in libc.so.6.so.0");

    _ctype_init_functions.push_back((ctype_init_fptr) ctype_init);
}

int SpyLoader::pthreadKeyCreate(pthread_key_t *key, void (*destructor)(void *)) noexcept {
    std::lock_guard lk(_pthreadKeyMutex);

    int res = _pthread_key_create_functions[0](key, destructor);
    if (res) {
        error_log("Pthread_key_create in base namespace failed " << strerror(res));
        return res;
    }

    for (uint32_t idx = 1; idx < _pthread_key_create_functions.size(); idx++) {
        pthread_key_t tmpKey;

        res = _pthread_key_create_functions[idx](&tmpKey, destructor);
        if (res) {
            error_log("Pthread_key_create in namespace " << idx << " failed " << strerror(res));
            return res;
        } else if (*key != tmpKey) {
            error_log("Incoherent thread key in namespace " << idx << " (expected : " << tmpKey << " / returned : " << *key << ")");
            return -1; // #FIXME find a better error code
        }
    }

    return 0;
}

int SpyLoader::pthreadKeyDelete(pthread_key_t key) noexcept {
    std::lock_guard lk(_pthreadKeyMutex);

    for(auto pthrread_key_del : _pthread_key_delete_functions){
        int res = pthrread_key_del(key);
        if (res) {
            error_log("Pthread_key_delete failed in one namespace " << strerror(res));
            return res;
        }
    }

    return 0;
}

void SpyLoader::ctypeInit() noexcept {
    for(auto f : _ctype_init_functions){
        f();
    }
}

void SpyLoader::initStaticTLS(struct link_map *lm) noexcept {
    for(auto f : _init_static_tls_functions){
        f(lm);
    }
}

Lmid_t SpyLoader::reserveNamespaceId(DynamicNamespace& dynamicNamespace) {
    // FIXME there is no guarantee -2 cannot be used as dynamic namespace id
    Lmid_t id = -2;

    if(!_avlNamespaceId.empty()){
        auto node = _avlNamespaceId.extract(_avlNamespaceId.begin());
        id = node.value();
        _usedNamespace.emplace(id, dynamicNamespace);
    }

    return id;
}

void SpyLoader::releaseNamespaceId(Lmid_t id) {
    auto node = _usedNamespace.extract(id);

    if(!node.empty()){
        _avlNamespaceId.insert(node.key());
    } else {
        error_log("Try to release unused namespace " << id);
    }
}

DynamicNamespace *SpyLoader::getCurrentNamespace(){
    void* retAddr =  __builtin_extract_return_addr(__builtin_return_address(0));

    Dl_info info;
    struct link_map* lm;

    if(dladdr1(retAddr, &info, (void**)(&lm), RTLD_DL_LINKMAP) == 0){
        error_log("Dladdr1 failed for "<< (void*) retAddr << ": " << dlerror());
        return nullptr;
    }

    if(_baseNamespace.isContaining(lm)){
        return &_baseNamespace;
    }


    auto it = std::find_if(_usedNamespace.cbegin(), _usedNamespace.cend(),[lm](const auto& pair){
        return pair.second.isContaining(lm);
    });
    
    if(it == _usedNamespace.cend()) {
        error_log("Called from a namespace that is not supposed to be used" << dlerror());
        return nullptr;
    }

    return &(it->second);

}

