#ifndef SPYTESTER_SPYLOADER_H
#define SPYTESTER_SPYLOADER_H

#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "DynamicNamespace.h"

class SpyLoader;

extern "C" {
    SpyLoader& getSpyLoader();
}

typedef struct list_head list_t;

class SpyLoader {

public :
    friend SpyLoader& getSpyLoader();

    int pthreadKeyCreate(pthread_key_t *key, void (*destructor)(void*)) noexcept;
    int pthreadKeyDelete(pthread_key_t key) noexcept;
    void ctypeInit() noexcept;
    void initStaticTLS(struct link_map* lm) noexcept;

    SpyLoader(const SpyLoader&) = delete;
    SpyLoader& operator=(const SpyLoader&) = delete;
    SpyLoader(SpyLoader&&) = delete;
    SpyLoader& operator=(SpyLoader&&) = delete;

    Lmid_t reserveNamespaceId(DynamicNamespace&);
    void releaseNamespaceId(Lmid_t id);

    DynamicNamespace* getCurrentNamespace();

private:
    SpyLoader();
    ~SpyLoader() = default;

    void createSpiedNamespaces(uint32_t nb);
    void updateWrappedFunctions(DynamicNamespace& spiedNamespace);

    using pthread_key_delete_fptr   = int (*)(pthread_key_t);
    using pthread_key_create_fptr   = int (*)(pthread_key_t *, void(*)(void *));
    using init_static_tls_fptr      = void(*)(struct link_map*);
    using ctype_init_fptr           = void(*)();

    std::mutex _pthreadKeyMutex;

    std::vector<pthread_key_delete_fptr> _pthread_key_delete_functions;
    std::vector<pthread_key_create_fptr> _pthread_key_create_functions;
    std::vector<ctype_init_fptr> _ctype_init_functions;
    std::vector<init_static_tls_fptr> _init_static_tls_functions;

    DynamicNamespace _baseNamespace;
    std::set<Lmid_t> _avlNamespaceId;
    std::map<Lmid_t, DynamicNamespace&> _usedNamespace;

    init_static_tls_fptr* _dlInitStaticTLS;
    list_t* _baseStackUserList;
    list_t* _defaultThreadStack;

    static SpyLoader* spyLoader;

    struct SpyLoaderInitializer{
        SpyLoaderInitializer();
    };

    static SpyLoaderInitializer spyLoaderInitializer;
};

#endif //SPYTESTER_SPYLOADER_H