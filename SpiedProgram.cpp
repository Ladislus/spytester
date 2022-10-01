#include "SpiedProgram.h"

#include <dlfcn.h>
#include <sys/mman.h>


#include <unistd.h>
#include <cstring>
#include <iostream>

static const std::string entryPointSymb("_start");
static const size_t stackSize = 1<<23;

uint32_t SpiedProgram::breakPointCounter = 0;

void defaultOnAddThread(SpiedThread& spiedThread)
{
    std::cout << "New thread "<< spiedThread.getTid() <<" created" <<std::endl;
}

void defaultOnRemoveThread(SpiedThread& spiedThread)
{
    std::cout << "Thread "<< spiedThread.getTid() <<" deleted" <<std::endl;
}

SpiedProgram::SpiedProgram(std::string&& progName, int argc, char* argv, char* envp)
: _progName(progName), _onThreadStart(defaultOnAddThread), _onThreadExit(defaultOnRemoveThread) {

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

    sleep(1); // #TODO create synchronisation with _tracer
}

SpiedProgram::~SpiedProgram(){
    std::cout << __FUNCTION__ << std::endl;
    delete _tracer;
}

void SpiedProgram::terminate() {
    for(auto & spiedThread : _spiedThreads)
    {
        spiedThread->terminate();
    }
}

void SpiedProgram::stop(){
    for(auto & spiedThread : _spiedThreads)
    {
        spiedThread->stop();
    }
}

void SpiedProgram::run() {
    for(auto & spiedThread : _spiedThreads)
    {
        spiedThread->resume(0);
    }
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

ProgParam *SpiedProgram::getProgParam() {
    return &_progParam;
}

const std::string &SpiedProgram::getProgName() {
    return _progName;
}

char *SpiedProgram::getStackTop() {
    return (char*)_stack+stackSize;
}


// Exec Breakpoint Management
BreakPoint *SpiedProgram::createBreakPoint(void *addr, std::string &&name) {
    _breakPoints.emplace_back(std::make_unique<BreakPoint>(*_tracer, std::move(name), addr));
    return _breakPoints.back().get();
}

BreakPoint *SpiedProgram::getBreakPointAt(void* addr) {
    BreakPoint* bp = nullptr;

    for(auto &breakPoint : _breakPoints)
    {
        if(breakPoint->getAddr() == addr){
            bp = breakPoint.get();
        }
    }

    return bp;
}

// Thread Management
SpiedThread *SpiedProgram::getSpiedThread(pid_t tid) {
    SpiedThread *spiedThread = nullptr;

    for (auto &ptr: _spiedThreads) {
        if (tid == ptr->getTid()) {
            spiedThread = ptr.get();
            break;
        }
    }
    return spiedThread;
}

void SpiedProgram::setOnThreadStart(void(*onThreadStart)(SpiedThread &)) {
    _onThreadStart = onThreadStart;
}

void SpiedProgram::setOnThreadExit(void (*onThreadExit)(SpiedThread &)) {
    _onThreadExit = onThreadExit;
}

SpiedThread *SpiedProgram::onThreadStart(pid_t tid) {
    _spiedThreads.emplace_back(std::make_unique<SpiedThread>(*this, *_tracer, tid));
    _onThreadStart(*_spiedThreads.back());
    return _spiedThreads.back().get();
}

void SpiedProgram::onThreadExit(pid_t tid) {
    auto it = _spiedThreads.begin();
    for (; it != _spiedThreads.end(); it++) {
        if ((*it)->getTid() == tid) break;
    }

    if (it != _spiedThreads.end()) {
        _onThreadExit(**it);
    } else {
        std::cerr << __FUNCTION__ << " : unknown thread " << tid << std::endl;
    }
}




