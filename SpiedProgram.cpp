#include "SpiedProgram.h"

#include <dlfcn.h>
#include <sys/mman.h>


#include <unistd.h>
#include <cstring>
#include <iostream>

static const std::string entryPointSymb("_start");
static const size_t stackSize = 1<<23;

uint32_t SpiedProgram::breakPointCounter = 0;

SpiedProgram::SpiedProgram(std::string&& progName, int argc, char* argv, char* envp)
: _progName(progName) {

    _progParam.argc = (uint64_t) argc;
    _progParam.argv = argv;
    _progParam.envp = envp;

    _handle = dlopen(_progName.c_str(), RTLD_NOW);
    if (_handle == nullptr) {
        std::cerr << __FUNCTION__ << " : dlopen failed for " << _progName << " : " << dlerror() << std::endl;
        throw std::invalid_argument("Invalid program name");
    }

    _progParam.entryPoint = dlsym(_handle, entryPointSymb.c_str());
    if (_progParam.entryPoint == nullptr) {
        std::cerr << __FUNCTION__ << " : dlsym failed for " << entryPointSymb << " : " << dlerror() << std::endl;
        dlclose(_handle);
        throw std::invalid_argument("Cannot find entry point");
    }

    _stack = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (_stack == MAP_FAILED) {
        std::cerr << __FUNCTION__ << " : stack allocation failed : " << strerror(errno) << std::endl;
        dlclose(_handle);
        throw std::invalid_argument("Cannot allocate stack");
    }

    _tracer = new Tracer(*this);

    sleep(2); // #TODO create synchronisation with _tracer
}

SpiedProgram::~SpiedProgram(){
    std::cout << __FUNCTION__ << std::endl;
    delete _tracer;
}

void SpiedProgram::exit() {}

void SpiedProgram::run() {
    SpiedThread& mainThread = _tracer->getMainThread();
    mainThread.resume();
}

BreakPoint* SpiedProgram::createBreakPoint(std::string &&symbName) {
    void *addr = dlsym(_handle, symbName.c_str());

    if (addr == nullptr) {
        std::cerr << __FUNCTION__ << " : dlsym failed for "
                  << symbName << " : " << dlerror() << std::endl;
        return nullptr;
    }

    return createBreakPoint(addr, std::move(symbName));
}

BreakPoint *SpiedProgram::createBreakPoint(void *addr, std::string &&name) {
     auto it = _breakPoints.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(addr),
                                    std::forward_as_tuple(*_tracer, std::move(name), addr)).first; //#TODO simplify

    return &it->second;
}

ProgParam *SpiedProgram::getProgParam() {
    return &_progParam;
}

const std::string &SpiedProgram::getProgName() {
    return _progName;
}

char *SpiedProgram::getStackTop() {
    return (char*)_stack+stackSize;
}

BreakPoint *SpiedProgram::getBreakPointAt(unsigned long long int addr) {
    BreakPoint* bp = nullptr;

    auto it = _breakPoints.find((void*)addr);
    if(it != _breakPoints.end())
    {
        bp = &(it->second);
    }

    return bp;
}



