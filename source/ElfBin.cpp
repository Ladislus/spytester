#include <fcntl.h>
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <iostream>
#include "../include/ElfBin.h"

ElfBin::ElfBin(void *handle) : _handle(handle){
    if(dlinfo(_handle, RTLD_DI_LINKMAP, &_lm) == -1){
        throw(std::invalid_argument(std::string(__FUNCTION__) + " : failed to get link map"));
    }

    _elfHeader = (Elf64_Ehdr*) _lm->l_addr;
    //_progHeader = (Elf64_Phdr*) _lm->l_addr + _elfHeader->e_phoff;
    _sectHeaderMapping = SectionMapping(_lm->l_name, _elfHeader->e_shoff, _elfHeader->e_shnum * _elfHeader->e_shentsize);
    _sectHeader = (Elf64_Shdr*)_sectHeaderMapping._sectionAddr;

    _shStrTable = (const char*)_sectHeader[_elfHeader->e_shstrndx].sh_addr;

    for(uint32_t idx = 0; idx < _elfHeader->e_shnum; idx++){
        if(idx == _elfHeader->e_shstrndx) continue;

        switch(_sectHeader[idx].sh_type){
            case SHT_SYMTAB:
                _symbolNb = _sectHeader[idx].sh_size / _sectHeader[idx].sh_entsize;
                _symtabMapping = SectionMapping(_lm->l_name, _sectHeader[idx].sh_offset, _sectHeader[idx].sh_size);
                _symTable = (Elf64_Sym*)_symtabMapping._sectionAddr;
                break;
            case SHT_STRTAB:
                if((_sectHeader[idx].sh_flags & SHF_ALLOC) == 0 &&
                    idx != _elfHeader->e_shstrndx)
                {
                    _strtabMapping = SectionMapping(_lm->l_name, _sectHeader[idx].sh_offset, _sectHeader[idx].sh_size);
                    _strTable = (const char*)_strtabMapping._sectionAddr;
                }
                break;
        }
    }

    for(Elf64_Dyn* it = _lm->l_ld; it->d_tag != DT_NULL; it++){
        switch (it->d_tag) {
            case DT_JMPREL:
                _dynamicVector.pltRela = (Elf64_Rela*) it->d_un.d_ptr;
                break;
            case DT_PLTRELSZ:
                _dynamicVector.pltSize = it->d_un.d_val;
            case DT_RELA:
                _dynamicVector.gotRela = (Elf64_Rela*) it->d_un.d_ptr;
                break;
            case DT_RELASZ:
                _dynamicVector.gotSize = it->d_un.d_val;
                break;

                /*
            case DT_SYMTAB:
                _symTable = (Elf64_Sym*) it->d_un.d_ptr;
                break;
            case DT_STRTAB:
                _strTable = (const char *) it->d_un.d_ptr;
                break; */
        }
    }
}

void *ElfBin::getSymbol(const std::string &symbName) const {
    void* symbolAddr = dlsym(_handle, symbName.c_str());

    // if failed to find the symbol in dynamic symbol
    if(symbolAddr == nullptr) {
        for (uint32_t idx = 0; idx < _symbolNb; idx++) {
            uint8_t type = ELF64_ST_TYPE(_symTable[idx].st_info);

            if ((type == STT_FUNC || type == STT_OBJECT) &&     // symbol at idx is function or object
                (_symTable[idx].st_shndx != 0) &&               // symbol at idx is defined in the binary
                symbName == &_strTable[_symTable[idx].st_name]) // symbol at idx name match symbName
            {
                //std::cout << __FUNCTION__ << " : " << symbName << " found " << std::endl;
                symbolAddr = (void *) (_lm->l_addr + _symTable[idx].st_value);
                break;
            }
        }
    }

    return symbolAddr;
}

ElfBin::SectionMapping::SectionMapping() : _mappingAddr(nullptr), _sectionAddr(nullptr), _size(0)
{}

ElfBin::SectionMapping::SectionMapping(const char* file, off_t offset, size_t size) {
    int fd = open(file, O_RDONLY);
    if(fd == -1){
        throw std::invalid_argument("Failed to open " + std::string(file) + " : " + strerror(errno));
    }

    off_t diff = offset % sysconf(_SC_PAGE_SIZE);
    _size = diff + size;

    _mappingAddr = mmap(nullptr, _size, PROT_READ, MAP_PRIVATE, fd, offset - diff);
    if(_mappingAddr == MAP_FAILED){
        close(fd);
        throw std::invalid_argument("Failed to mmap memory for " + std::string(file) + " : " + strerror(errno));
    }
    _sectionAddr = (void*)((uint64_t)_mappingAddr + diff);

    close(fd);
}

ElfBin::SectionMapping::~SectionMapping() {
    if(_mappingAddr != nullptr){
        munmap(_mappingAddr, _size);
    }
}

ElfBin::SectionMapping &ElfBin::SectionMapping::operator=(ElfBin::SectionMapping &&other) noexcept {
    if(this != &other) {
        if(_mappingAddr != nullptr){
            munmap(_mappingAddr, _size);
        }

        _mappingAddr = other._mappingAddr;
        _sectionAddr = other._sectionAddr;
        _size = other._size;

        other._mappingAddr = nullptr;
    }
    return *this;
}

ElfBin::SectionMapping::SectionMapping(ElfBin::SectionMapping &&other) noexcept {
    _mappingAddr = other._mappingAddr;
    _sectionAddr = other._sectionAddr;
    _size = other._size;

    other._mappingAddr = nullptr;
}
