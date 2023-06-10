#include <iostream>
#include <cstring>
#include <algorithm>
#include <list>
#include "../include/SpyLoader.h"

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
    std::cout << __FUNCTION__ << std::endl;
    auto& loader = getSpyLoader();
    return loader.pthreadKeyCreate(key, destructor);
}

int pthread_key_delete(pthread_key_t key) noexcept{
    std::cout << __FUNCTION__ << std::endl;
    auto& loader = getSpyLoader();
    return loader.pthreadKeyDelete(key);
}

void __ctype_init() noexcept{
    std::cout << __FUNCTION__ << std::endl;
    auto& loader = getSpyLoader();
    return loader.ctypeInit();
}

void dl_init_static_tls(struct link_map* lm) noexcept{
    std::cout << __FUNCTION__ << std::endl;
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
        SpyLoader::spyLoader->createSpiedNamespaces(namespaceNb);
    } else {
        handle = dlmopen(LM_ID_BASE, "libSpyLoader.so", RTLD_LAZY | RTLD_NOLOAD);
        if(handle == nullptr){
            std::cerr << __FUNCTION__ << " : dlmopen libSpyLoader.so failed : " << dlerror() << std::endl;
            std::abort();
        }
        auto getMasterSpyLoader =  dlsym(handle, "getSpyLoader");
        SpyLoader::spyLoader = &((SpyLoader& (*)())getMasterSpyLoader)();
    }

    std::cout << __FUNCTION__ << " : spyLoader in namespace " << lmid << std::endl;
}

SpyLoader &getSpyLoader() {
    return *SpyLoader::spyLoader;
}

SpyLoader* SpyLoader::spyLoader;

SpyLoader::SpyLoader() : _baseNamespace() {

    // load libpthread in the current namespace
    DynamicModule* libpthread = _baseNamespace.load("libpthread.so.0");
    if(libpthread == nullptr){
        std::cerr << __FUNCTION__ <<" : failed to load libpthread.so.0 in base namespace" << std::endl;
        std::abort();
    }

    void* pthread_init_static_tls = libpthread->getSymbol("__pthread_init_static_tls");
    if(pthread_init_static_tls == nullptr) {
        std::cerr << __FUNCTION__ << " : failed to find __pthread_init_static_tls in libpthread.so.0 in base namespace"
                  << std::endl;
        std::abort();
    }

    auto rtld_global_it = (uint64_t*)&_rtld_global;
    // #FIXME check we are not going past the end of _rtld_global
    while(*rtld_global_it != (uint64_t)pthread_init_static_tls){
        rtld_global_it++;
    }
    _dlInitStaticTLS = (init_static_tls_fptr*) rtld_global_it;

    // get __stack_user which is used by libpthread to keep track of threads managed by the library
    _baseStackUserList = (list_t*) libpthread->getSymbol("__stack_user");
    if(_baseStackUserList == nullptr){
        std::cerr << __FUNCTION__ << " : failed to find __stack_user in libpthread.so.0 in base namespace" << std::endl;
        std::abort();
    }
    _defaultThreadStack = _baseStackUserList->next;

    updateWrappedFunctions(_baseNamespace);

}

void SpyLoader::createSpiedNamespaces(uint32_t nb) {

    if(!_avlNamespaceId.empty() || !_usedNamespace.empty() ){
        std::cerr << __FUNCTION__ <<" should only be called once" <<std::endl;
        return;
    }

    std::list<DynamicNamespace> namespaces;

    for(uint32_t i = 0; i < nb; i++){
        void* handle = dlmopen(LM_ID_NEWLM, "libSpyLoader.so", RTLD_LAZY);

        if(handle == nullptr){
            std::cerr << __FUNCTION__ <<" : failed to load libSpyLoader.so : "<< dlerror() << std::endl;
            continue;
        }

        Lmid_t id;
        if(dlinfo(handle, RTLD_DI_LMID, &id) != 0){
            std::cerr << __FUNCTION__ <<" : dlinfo failed : " << dlerror() << std::endl;
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
    if(libpthread == nullptr){
        std::cerr << __FUNCTION__ <<" : failed to load libpthread.so.0" << std::endl;
        std::abort();
    }

    // init_static_tls
    void* pthread_init_static_tls = libpthread->getSymbol("__pthread_init_static_tls");
    if(pthread_init_static_tls == nullptr) {
        std::cerr << __FUNCTION__ << " : failed to find __pthread_init_static_tls in libpthread.so.0" << std::endl;
        std::abort();
    }
    _init_static_tls_functions.push_back((init_static_tls_fptr) pthread_init_static_tls);

    // override _rtld_global._dl_init_static_tls with our custom function
    *_dlInitStaticTLS = &dl_init_static_tls;

    // pthread_key_delete
    auto pthread_key_del = libpthread->getSymbol("pthread_key_delete");
    if(pthread_key_del == nullptr) {
        std::cerr << __FUNCTION__ << " : failed to find pthread_key_delete in libpthread.so.0" << std::endl;
        std::abort();
    }
    _pthread_key_delete_functions.push_back( (pthread_key_delete_fptr) pthread_key_del);

    // pthread_key_create
    auto pthread_key_cre = libpthread->getSymbol("pthread_key_create");
    if(pthread_key_cre == nullptr) {
        std::cerr << __FUNCTION__ << " : failed to find pthread_key_create in libpthread.so.0" << std::endl;
        std::abort();
    }
    _pthread_key_create_functions.push_back((pthread_key_create_fptr) pthread_key_cre);

    // repair __stack_user
    auto stackUserList = (list_t*) libpthread->getSymbol("__stack_user");
    if(stackUserList == nullptr) {
        std::cerr << __FUNCTION__ << " : failed to find __stack_user in libpthread.so.0" << std::endl;
        std::abort();
    }

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
    if(libc == nullptr){
        std::cerr << __FUNCTION__ <<" : failed to load libc.so.6" << std::endl;
        std::abort();
    }

    // __ctype_init
    void* ctype_init = libc->getSymbol("__ctype_init");
    if(ctype_init == nullptr) {
        std::cerr << __FUNCTION__ << " : failed to find __ctype_init in libc.so.6.so.0" << std::endl;
        std::abort();
    }
    _ctype_init_functions.push_back((ctype_init_fptr) ctype_init);
}

int SpyLoader::pthreadKeyCreate(pthread_key_t *key, void (*destructor)(void *)) noexcept {
    std::lock_guard lk(_pthreadKeyMutex);

    int res = _pthread_key_create_functions[0](key, destructor);
    if (res != 0) {
        std::cerr << __FUNCTION__ << " : pthread_key_create in base namespace failed : "
                  << strerror(res) << std::endl;
        return res;
    }

    for (uint32_t idx = 1; idx < _pthread_key_create_functions.size(); idx++) {
        pthread_key_t tmpKey;

        res = _pthread_key_create_functions[idx](&tmpKey, destructor);
        if (res != 0) {
            std::cerr << __FUNCTION__ << " : pthread_key_create in namespace " << idx << " failed : "
                      << strerror(res) << std::endl;
            return res;
        } else if (*key != tmpKey) {
            std::cerr << __FUNCTION__ << " : incoherent thread key in namespace " << idx << " (expected : " << tmpKey
                      << " / returned : " << *key << ")" << std::endl;
            return -1; // #FIXME find a better error code
        }
    }

    return 0;
}

int SpyLoader::pthreadKeyDelete(pthread_key_t key) noexcept {
    std::lock_guard lk(_pthreadKeyMutex);

    for(auto pthrread_key_del : _pthread_key_delete_functions){
        int res = pthrread_key_del(key);
        if (res != 0) {
            std::cerr << __FUNCTION__ << " : pthread_key_delete failed in one namespace : "
                      << strerror(res) << std::endl;
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
        std::cerr << __FUNCTION__ << " : try to release unused namespace "<< id << std::endl;
    }
}

DynamicNamespace *SpyLoader::getCurrentNamespace(){
    void* retAddr = __builtin_return_address(0);

    Dl_info info;
    struct link_map* lm;

    if(dladdr1(retAddr, &info, (void**)(&lm), RTLD_DL_LINKMAP) == 0){
        std::cerr << __FUNCTION__ << " : dladdr1 failed for "<< (void*) retAddr <<" : " << dlerror() << std::endl;
        return nullptr;
    }

    if(_baseNamespace.isContaining(lm)){
        return &_baseNamespace;
    }

    auto it = std::find_if(_usedNamespace.cbegin(), _usedNamespace.cend(),[lm](const auto& pair){
        return pair.second.isContaining(lm);
    });

    if(it == _usedNamespace.cend()) {
        std::cerr << __FUNCTION__ << " is called from a namespace that is not supposed to be used" << dlerror()
                  << std::endl;
        return nullptr;
    }

    return &(it->second);
}

