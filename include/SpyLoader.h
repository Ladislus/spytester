#ifndef SPYTESTER_SPYLOADER_H
#define SPYTESTER_SPYLOADER_H

#include <optional>
#include <thread>
#include <mutex>
#include <vector>
#include "SpiedNamespace.h"

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

    SpiedNamespace& reserveNamespace();
    SpiedNamespace& releaseNamespace();

    SpyLoader(const SpyLoader&) = delete;
    SpyLoader& operator=(const SpyLoader&) = delete;
    SpyLoader(SpyLoader&&) = delete;
    SpyLoader& operator=(SpyLoader&&) = delete;

private:
    SpyLoader();
    ~SpyLoader() = default;

    void createNamespaces();
    void updateWrappedFunctions(SpiedNamespace& spiedNamespace);

    using pthread_key_delete_fptr   = int (*)(pthread_key_t);
    using pthread_key_create_fptr   = int (*)(pthread_key_t *, void(*)(void *));
    using init_static_tls_fptr      = void(*)(struct link_map*);
    using ctype_init_fptr           = void(*)();

    std::mutex _pthreadKeyMutex;

    std::vector<pthread_key_delete_fptr> _pthread_key_delete_functions;
    std::vector<pthread_key_create_fptr> _pthread_key_create_functions;
    std::vector<ctype_init_fptr> _ctype_init_functions;
    std::vector<init_static_tls_fptr> _init_static_tls_functions;

    SpiedNamespace _baseNamespace;
    std::vector<SpiedNamespace> _spiedNamespaces;

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
