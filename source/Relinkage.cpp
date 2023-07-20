#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#include "DynamicModule.h"
#include "Relinkage.h"
#include "Logger.h"

Relinkage::Relinkage(DynamicModule &source, DynamicModule &destination): 
    _source(source),
    _destination(destination),
    _validity(true) {

    info_log(source.getName() << " -> " << destination.getName());

    auto findRela = [this](uint32_t type, const std::string& name, uint64_t* relaAddr){
        if(type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT){

            void* symbAddr = _destination.getDynamicSymbol(name);
            if(symbAddr)
                _relocations.emplace_back(relaAddr, (uint64_t)symbAddr);
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
        if((uint64_t) rela.first < lowestRelaAddr)
            lowestRelaAddr = (uint64_t) rela.first;

        if((uint64_t) rela.first > lowestRelaAddr)
            highestRelaAddr = (uint64_t) rela.first;
    }

    if(lowestRelaAddr < highestRelaAddr) {
        lowestRelaAddr -= lowestRelaAddr % sysconf(_SC_PAGE_SIZE); // align lowestRelaAddr to page size

        // Change memory protection to write relocation in .got.plt and .got
        if(-1 == mprotect((void*)lowestRelaAddr, highestRelaAddr - lowestRelaAddr, PROT_WRITE | PROT_READ)) {
            error_log("Failed to change memory protection RW : " << lowestRelaAddr << " - " << highestRelaAddr << ": " << strerror(errno));
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
            error_log("Failed to change memory protection RO : " << lowestRelaAddr << " - " << highestRelaAddr << ": " << strerror(errno));
        }
    }
}