#include <csignal>
#include <dlfcn.h>
#include <elf.h>
#include <future>
#include <iostream>
#include <link.h>

#include "Logger.h"

void start(void* entryPoint, int argc, char* argv[], char* envp[]){
    uint64_t* sp;
    __asm__ volatile (" movq %%rsp, %0" : "=r"(sp)::);

    char** env = envp;
    for(; *env != nullptr; env++); // set env point to the last environment variable

    uint64_t wordsToPush = static_cast<uint64_t>(argc + 4 + (env - envp));

    // guarantee that the stack pointer will be 16-bytes aligned when entryPoint is called
    if((uint64_t)(sp + wordsToPush) & 0xF){
        sp--;
        *sp = 0LU;
    }

    // Push empty auxiliary vector onto the stack
    sp--;
    *sp = 0LU;

    // Push environment pointers onto the stack
    for(; env >= envp; env--){
        sp--;
        *sp = (uint64_t)*env;
    }

    // Push argument pointers onto the stack
    sp--;
    *sp = 0LU;

    for(int i = argc - 1; i>=0; i--){
        sp--;
        *sp = (uint64_t)argv[i];
    }

    // Push argc onto the stack
    sp--;
    *sp = static_cast<uint64_t>(argc);

    //initialize register and jump to entry point
    __asm__ volatile(
            "xor %%rbp, %%rbp \n"
            "xor %%rdx, %%rdx \n"
            "movq %0, %%rsp \n"
            "jmp *%1 \n"
            ::"r"(sp), "r"(entryPoint): "%rdx");

    // dummy call to force gcc to decrement sp at start entry
    std::exit(0);
}

extern "C" {
    void preload(int argc, char* argv[], char* envp[], std::promise<void*> execHandle);
}

void load(int argc, char* argv[], char* envp[], std::promise<void*> execHandle);

void preload(int argc, char* argv[], char* envp[], std::promise<void*> execHandle){

    auto loader = std::thread(load, argc, argv, envp, std::move(execHandle));
    loader.detach();

    raise(SIGSTOP);
}

void load(int argc, char* argv[], char* envp[], std::promise<void*> execHandle){

    void* handle = dlopen( argv[0], RTLD_NOW);
    if (handle == nullptr) {
        error_log("Dlopen failed for " << argv[0] << " : " << dlerror());
        return;
    }

    struct link_map* lm;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) == -1) {
        error_log("Dlinfo failed RTLD_DI_LINKMAP : " << dlerror());
        return;
    }

    auto elfHdr = (Elf64_Ehdr*)lm->l_addr;
    Elf64_Addr baseAddr = lm->l_addr;
    void* entryPoint = (void*)(baseAddr + elfHdr->e_entry);
    info_log("EntryPoint = " << entryPoint);
    execHandle.set_value(handle);

    raise(SIGSTOP);

    start(entryPoint, argc, argv, envp);
}