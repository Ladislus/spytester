#ifndef TEST_ELFBIN_H
#define TEST_ELFBIN_H

#include <link.h>
#include <string>

class ElfBin {
    struct SectionMapping{
        SectionMapping();
        SectionMapping(const char* file, off_t offset, size_t size);
        ~SectionMapping();
        SectionMapping(const SectionMapping&) = delete;
        SectionMapping& operator=(const SectionMapping&) = delete;
        SectionMapping(SectionMapping&& other) noexcept ;
        SectionMapping& operator=(SectionMapping&& other) noexcept ;
        void* _sectionAddr;
    private:
        void* _mappingAddr;
        size_t _size;
    };

    void* _handle;
    struct link_map* _lm;

    Elf64_Ehdr* _elfHeader;
    //Elf64_Phdr* _progHeader;
    Elf64_Shdr* _sectHeader;

    SectionMapping _sectHeaderMapping;
    SectionMapping _symtabMapping;
    SectionMapping _strtabMapping;

    Elf64_Sym* _symTable;
    uint32_t _symbolNb;
    const char* _shStrTable;
    const char* _strTable;

    struct DynamicVector{
        Elf64_Rela* pltRela;
        size_t pltSize;
        Elf64_Rela* gotRela;
        size_t gotSize;
    } _dynamicVector;

public:
    explicit ElfBin(void* handle);
    void* getSymbol(const std::string& symbName) const;
};


#endif //TEST_ELFBIN_H
