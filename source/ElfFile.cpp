#include "../include/ElfFile.h"
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <climits>

ElfFile::ElfFile(const std::string &filePath) : _filePath(filePath) {
    _fd = open(_filePath.c_str(), O_RDONLY);
    if(_fd == -1) {
        std::cerr << "BOUGN : " << filePath << std::endl;
        throw std::invalid_argument(
                std::string(__FUNCTION__) + " : Failed to load " + _filePath + " : " + strerror(errno));
    }

    // Read elf header
    size_t bytesRead = read(_fd, &_elfHeader, sizeof(_elfHeader));
    if(bytesRead != sizeof(_elfHeader)){
        close(_fd);
        throw std::invalid_argument(
                std::string(__FUNCTION__) + " : Failed to read elf header of " + _filePath + " : " + strerror(errno));
    }

    // IMPROVE : check elf magic number

    if(lseek(_fd, _elfHeader.e_shoff, SEEK_SET) != _elfHeader.e_shoff) {
        close(_fd);
        throw std::invalid_argument(
                std::string(__FUNCTION__) + " : Failed to go to section header offset in " + _filePath + " : " +
                strerror(errno));
    }

    // read section header
    _sectHeader.resize(_elfHeader.e_shnum);
    bytesRead = read(_fd, _sectHeader.data(), _elfHeader.e_shnum * _elfHeader.e_shentsize);

    if(bytesRead != _elfHeader.e_shnum * _elfHeader.e_shentsize){
        close(_fd);
        throw std::invalid_argument(
                std::string(__FUNCTION__) + " : Failed to read section header of " + _filePath + " : " + strerror(errno));
    }
}

ElfFile::~ElfFile() {
    close(_fd);
}

void ElfFile::readSection(Elf64_Shdr &sect, void *buf) {
    if(lseek(_fd, sect.sh_offset, SEEK_SET) != sect.sh_offset) {
        throw std::invalid_argument(
                std::string(__FUNCTION__) + "Failed to go to section offset in " + _filePath + " : " +
                strerror(errno));
    }

    size_t bytesRead = read(_fd, buf, sect.sh_size);

    if(bytesRead != sect.sh_size) {
        throw std::invalid_argument(
                std::string(__FUNCTION__) + "Failed to read section of " + _filePath + " : " +
                strerror(errno));
    }
}

const std::vector<Elf64_Dyn> &ElfFile::getDynamic() {
    if(!_dynamic.has_value()){
        auto& v = _dynamic.emplace();

        for(auto& section : _sectHeader) {
            if(section.sh_type == SHT_DYNAMIC) {
                v.resize(section.sh_size / section.sh_entsize);
                readSection(section, v.data());
                break;
            }
        }
    }

    return _dynamic.value();
}

const std::vector<Elf64_Sym> &ElfFile::getSymTab() {
    if(!_symtab.has_value()){
        auto& v = _symtab.emplace();

        for(auto& section : _sectHeader) {
            if(section.sh_type == SHT_SYMTAB) {
                v.resize(section.sh_size / section.sh_entsize);
                readSection(section, v.data());
                break;
            }
        }
    }

    return _symtab.value();
}

const std::vector<Elf64_Sym> &ElfFile::getDynSymTab() {
    if(!_dynsym.has_value()){
        auto& v = _dynsym.emplace();

        for(auto& section : _sectHeader) {
            if(section.sh_type == SHT_DYNSYM) {
                v.resize(section.sh_size / section.sh_entsize);
                readSection(section, v.data());
                break;
            }
        }
    }

    return _dynsym.value();
}

const std::vector<char> &ElfFile::getStrTab() {
    if(!_strtab.has_value()){
        auto& v = _strtab.emplace();

        for(uint32_t idx = 0; idx < _sectHeader.size(); idx++) {
            auto& section = _sectHeader[idx];

            if(section.sh_type == SHT_STRTAB && (section.sh_flags & SHF_ALLOC) == 0 && idx != _elfHeader.e_shstrndx) {
                v.resize(section.sh_size);
                readSection(section, v.data());
                break;
            }
        }
    }

    return _strtab.value();
}

const std::vector<char> &ElfFile::getDynStrTab() {
    if(!_dynstr.has_value()){
        auto& v = _dynstr.emplace();

        for(uint32_t idx = 0; idx < _sectHeader.size(); idx++) {
            auto& section = _sectHeader[idx];

            if(section.sh_type == SHT_STRTAB && (section.sh_flags & SHF_ALLOC) != 0 && idx != _elfHeader.e_shstrndx) {
                v.resize(section.sh_size);
                readSection(section, v.data());
                break;
            }
        }
    }

    return _dynstr.value();
}

const std::vector<Elf64_Rela> &ElfFile::getRela() {
    if(!_rela.has_value()){
        auto& v = _rela.emplace();

        for(auto& section : _sectHeader) {
            if(section.sh_type == SHT_RELA) {
                size_t prevSize = v.size();
                v.resize(section.sh_size / section.sh_entsize + prevSize);
                readSection(section, v.data()+prevSize);
            }
        }
    }

    return _rela.value();
}

ElfFile &ElfFile::getElfFile(const std::string &filePath) {
    decltype(elfFiles.begin()) it;

    if(filePath.empty()){
        char buffer[PATH_MAX];
        size_t len = readlink("/proc/self/exe", buffer, sizeof(buffer));

        if(len == -1) {
            throw std::invalid_argument(
                    std::string(__FUNCTION__) + " : failed to get executable path : " + strerror(errno));
        }
        buffer[len] = '\0';

        it = elfFiles.emplace(buffer, buffer).first;

    } else {
        it = elfFiles.emplace(filePath ,filePath).first;
    }
    return it->second;
}

Elf64_Addr ElfFile::getEntryPoint() const {
    return _elfHeader.e_entry;
}

const std::vector<Elf64_Shdr> &ElfFile::getShdr() {
    return _sectHeader;
}

// elfFiles are used by SpyLoader constructor, so it must be initialized before other static variables with
// default init priority (65535)
std::map<std::string, ElfFile> ElfFile::elfFiles __attribute__((init_priority(65534)));
