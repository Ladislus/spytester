#ifndef SPYTESTER_DYNAMICNAMESPACE_H
#define SPYTESTER_DYNAMICNAMESPACE_H


#include <map>

#include "DynamicModule.h"

class SpyLoader;

class DynamicNamespace {
public :
    friend SpyLoader;

    DynamicNamespace(int argc, const char* argv[], char **envp);
    ~DynamicNamespace();

    DynamicNamespace(const DynamicNamespace&) = delete;
    DynamicNamespace& operator=(const DynamicNamespace&) = delete;

    DynamicNamespace(DynamicNamespace&&) = default;
    DynamicNamespace& operator=(DynamicNamespace&&) = default;

    DynamicModule* load(const std::string& binName);
    void unload(const std::string& binName);

    bool iterateOverModule(const std::function<bool(DynamicModule&)>& f);

    static void createMainThread(DynamicNamespace* ns);

    // improve remove this method and improve WrappedFunction class
    void* convertDynSymbolAddr(void* addr) const;

private:

    DynamicNamespace();
    bool isContaining(struct link_map* lm) const;

    Lmid_t _id;
    struct link_map* _lm;

    const int _argc;
    const char** const _argv;
    char** const _envp ;

    std::optional<DynamicModule> _executable;
    DynamicModule _loader;
    std::map<std::string, DynamicModule> _dynamicLib;

    decltype(&DynamicNamespace::createMainThread) _createMainThread;

    void loadExecutable();
    void syncModules();
};


#endif //SPYTESTER_DYNAMICNAMESPACE_H
