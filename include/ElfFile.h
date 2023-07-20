#ifndef SPYTESTER_ELFFILE_H
#define SPYTESTER_ELFFILE_H


#include <elf.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

class ElfFile {
public:
    explicit ElfFile(const std::string& filePath);
    ~ElfFile();

    Elf64_Addr getEntryPoint() const;
    const std::vector<Elf64_Dyn>& getDynamic();
    const std::vector<Elf64_Sym>& getSymTab();
    const std::vector<Elf64_Sym>& getDynSymTab();
    const std::vector<char>& getStrTab();
    const std::vector<char>& getDynStrTab();
    const std::vector<Elf64_Rela>& getRela();
    const std::vector<Elf64_Shdr>& getShdr();

    static ElfFile& getElfFile(const std::string& filePath);

private:
    void readSection(Elf64_Shdr &sect, void *buf);

    static std::map<std::string, ElfFile> elfFiles;

    const std::string& _filePath;
    int _fd;

    Elf64_Ehdr _elfHeader;
    std::vector<Elf64_Shdr> _sectHeader;

    std::optional<std::vector<Elf64_Dyn>> _dynamic;
    std::optional<std::vector<Elf64_Sym>> _symtab;
    std::optional<std::vector<Elf64_Sym>> _dynsym;
    std::optional<std::vector<char>> _strtab;
    std::optional<std::vector<char>> _dynstr;
    std::optional<std::vector<Elf64_Rela>> _rela;

};
#endif //SPYTESTER_ELFFILE_H
