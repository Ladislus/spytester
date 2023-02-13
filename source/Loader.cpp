#include <dlfcn.h>
#include <iostream>
#include <elf.h>
#include <link.h>
#include <csignal>

#include <future>

void start(void* entryPoint, int argc, char* argv[], char* envp[]){
    uint64_t* sp;
    __asm__ volatile (" movq %%rsp, %0" : "=r"(sp)::);

    char** env = envp;
    for(; *env != nullptr; env++); // set env point to the last environment variable

    uint64_t wordsToPush = argc + 4 + (env-envp);

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
    *sp = argc;

    //initialize register and jump to entry point
    __asm__ volatile(
            "xor %%rbp, %%rbp \n"
            "xor %%rdx, %%rdx \n"
            "movq %0, %%rsp \n"
            "jmp *%1 \n"
            ::"r"(sp), "r"(entryPoint): "%rdx");

    // dummy call to force gcc to decrement sp at start entry
    exit(0);
}

extern "C"{
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
        std::cerr << __FUNCTION__ << " : dlopen failed for " << argv[0] << " : " << dlerror() << std::endl;
        return;
    }

    struct link_map* lm;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) == -1) {
        std::cerr << __FUNCTION__ << " : dlinfo failed RTLD_DI_LINKMAP : " << dlerror() << std::endl;
        return;
    }

    auto elfHdr = (Elf64_Ehdr*)lm->l_addr;
    Elf64_Addr baseAddr = lm->l_addr;
    void* entryPoint = (void*)(baseAddr + elfHdr->e_entry);
    std::cout << "entryPoint = "<< entryPoint << std::endl;
    execHandle.set_value(handle);

    raise(SIGSTOP);

    start(entryPoint, argc, argv, envp);
}