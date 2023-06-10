#include "../include/Relinkage.h"
#include "../include/DynamicModule.h"

Relinkage::Relinkage(DynamicModule &source, DynamicModule &destination)
: _source(source), _destination(destination), _validity(true)
{
    auto relinkSymbol = [&destination,this](uint32_t type, const std::string& name, uint64_t* relaAddr){
        if(type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT){

            void* symbAddr = destination.getDynamicSymbol(name);
            if(symbAddr != nullptr){
                *relaAddr = (uint64_t)symbAddr;
                _previousRelocations.emplace_back(relaAddr, *relaAddr);
            }
        }
        return false;
    };

    _source.iterateOverRelocations(relinkSymbol);
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
        for(auto& relocation : _previousRelocations){
            *(relocation.first) = relocation.second;
        }

    }
}