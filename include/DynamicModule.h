#ifndef TEST_ELFBIN_H
#define TEST_ELFBIN_H


#include <functional>
#include <link.h>
#include <map>
#include <set>
#include <string>

#include "ElfFile.h"
#include "Relinkage.h"

class DynamicModule {
private:

    using LinkMap = struct link_map;

    const std::string _name;
    void* _handle;
    LinkMap* _lm;
    ElfFile& _elf;

    std::set<Relinkage*> _inRelinkages;
    std::map<std::string, Relinkage> _outRelinkages;

public:
    DynamicModule(const std::string &name, Lmid_t id);
    ~DynamicModule();

    [[nodiscard]] const std::string& getName() const;
    [[nodiscard]] void* getDynamicSymbol(const std::string& symbName) const;
    [[nodiscard]] void* getSymbol(const std::string& symbName) const;
    void* getSymbol(void* symbolPtr) const;
    [[nodiscard]] void* getEntryPoint() const;

    void iterateOverRelocations(const std::function<bool(uint32_t, const std::string&, uint64_t*)>& f);

    void relink(DynamicModule& module);
    void unrelink(const std::string& libName);

    static std::string getMangledName(void* symbolPtr);

    // Should only be used Relinkage
    void addInRelinkage(Relinkage& relinkage);
    void removeInRelinkage(Relinkage& relinkage);
};


#endif //TEST_ELFBIN_H
