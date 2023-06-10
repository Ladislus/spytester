#include <iostream>
#include <sys/uio.h>
#include <unistd.h>
#include <cstring>
#include <sys/mman.h>
#include "../include/Relinkage.h"
#include "../include/DynamicModule.h"

Relinkage::Relinkage(DynamicModule &source, DynamicModule &destination)
: _source(source), _destination(destination), _validity(true)
{
    std::cout << __FUNCTION__ <<" : "<< source.getName() << " -> " << destination.getName() << std::endl;

    auto findRela = [this](uint32_t type, const std::string& name, uint64_t* relaAddr){
        if(type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT){

            void* symbAddr = _destination.getDynamicSymbol(name);
            if(symbAddr != nullptr){
                _relocations.emplace_back(relaAddr, (uint64_t)symbAddr);
            }
        }
        return true;
    };

    _source.iterateOverRelocations(findRela);
    writeRelocations();
    _destination.addInRelinkage(*this);
}

Relinkage::~Relinkage() {
    invalidate();
    _destination.removeInRelinkage(*this);
}

void Relinkage::invalidate() {
    if(_validity) {
        _validity = false;

        // restore previous relocations
        writeRelocations();

    }
}

void Relinkage::writeRelocations() {
    uint64_t lowestRelaAddr = -1;
    uint64_t highestRelaAddr = 0;

    for(auto& rela : _relocations){
        if((uint64_t) rela.first < lowestRelaAddr){
            lowestRelaAddr = (uint64_t) rela.first;
        }
        if((uint64_t) rela.first > lowestRelaAddr){
            highestRelaAddr = (uint64_t) rela.first;
        }
    }

    if(lowestRelaAddr < highestRelaAddr){
        lowestRelaAddr -= lowestRelaAddr % sysconf(_SC_PAGE_SIZE); // align lowestRelaAddr to page size

        // Change memory protection to write relocation in .got.plt and .got
        if(-1 == mprotect((void*)lowestRelaAddr, highestRelaAddr - lowestRelaAddr, PROT_WRITE | PROT_READ)) {
            std::cerr << __FUNCTION__ << " : failed to change memory protection RW : " << lowestRelaAddr << " - "
                      << highestRelaAddr <<" : "<< strerror(errno) << std::endl;
            return;
        }

        for(auto& rela : _relocations){
            uint64_t newAddr = rela.second;

            //save old addr
            rela.second = *rela.first;

            // write new addr
            *rela.first = newAddr;
        }

        // Restore memory protection
        if(-1 == mprotect((void*)lowestRelaAddr, highestRelaAddr - lowestRelaAddr, PROT_READ)){
            std::cerr << __FUNCTION__ << " : failed to change memory protection RO : " << lowestRelaAddr << " - "
                      << highestRelaAddr <<" : "<< strerror(errno) << std::endl;
        }
    }
}