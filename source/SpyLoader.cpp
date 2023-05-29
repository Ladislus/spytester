#include <iostream>
#include <cstring>
#include "../include/SpyLoader.h"

#define NAMESPACE_NB 10

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
        SpyLoader::spyLoader = new SpyLoader();
        SpyLoader::spyLoader->createNamespaces();
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

SpyLoader::SpyLoader() : _baseNamespace(true) {

    // load libpthread in the current namespace
    ElfBin* libpthread = _baseNamespace.open("libpthread.so.0");
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

void SpyLoader::createNamespaces() {

    if(!_spiedNamespaces.empty()){
        std::cerr << __FUNCTION__ <<" should only be called once" <<std::endl;
        return;
    }

    std::cout << __FUNCTION__ << " start creating " << NAMESPACE_NB-1 << " additional namespace" << std::endl;

    for(uint32_t id = 1; id < NAMESPACE_NB; id++){
        auto& spiedNamespace = _spiedNamespaces.emplace_back(false);

        if(spiedNamespace.open("libSpyLoader.so") == nullptr){
            std::cerr << __FUNCTION__ << " : failed to load libSpyLoader.so in a new namespace" << std::endl;
            std::abort();
        }

        updateWrappedFunctions(spiedNamespace);
    }

    *_dlInitStaticTLS = &dl_init_static_tls;
}

void SpyLoader::updateWrappedFunctions(SpiedNamespace &spiedNamespace) {
    // libpthread
    ElfBin* libpthread = spiedNamespace.open("libpthread.so.0");
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
    ElfBin* libc = spiedNamespace.open("libc.so.6");
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
