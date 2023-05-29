#include "../../include/SpiedProgram.h"
#include <cstdlib>
#include <sys/sysinfo.h>

void dump_info(){
    void* handle_ld_linux;
    void* handle_libc;

    struct link_map* lm_ld_linux;
    struct link_map* lm_libc;

    void* tls_get_addr;

    std::cout << "\nCurrent namespace :"<< std::endl;

    handle_ld_linux = dlmopen(0, "ld-linux-x86-64.so.2", RTLD_LAZY | RTLD_NOLOAD);
    if(handle_ld_linux == nullptr){
        std::cerr << __FUNCTION__ << " : dlmopen 0 failed : "<< dlerror() << std::endl;
        return;
    }

    handle_libc = dlmopen(0, "libc.so.6", RTLD_NOW);
    if(handle_libc == nullptr){
        std::cerr << __FUNCTION__ << " : dlmopen 0 failed : "<< dlerror() << std::endl;
        return;
    }

    if(dlinfo(handle_ld_linux, RTLD_DI_LINKMAP, &lm_ld_linux) == -1){
        std::cerr << __FUNCTION__ << " : dlinfo 0 failed : "<< dlerror() << std::endl;
        return;
    };

    if(dlinfo(handle_libc, RTLD_DI_LINKMAP, &lm_libc) == -1){
        std::cerr << __FUNCTION__ << " : dlinfo 0 failed : "<< dlerror() << std::endl;
        return;
    };

    tls_get_addr = dlsym(handle_ld_linux, "__tls_get_addr");

    std::cout << "Base addr = " << (void*) lm_ld_linux->l_addr << std::endl;
    std::cout << "tls_get_addr = "<< tls_get_addr << std::endl;
    std::cout << "tls_get_addr (computed) = " << (void*)(lm_ld_linux->l_addr + 0x19b70) << std::endl;
    std::cout << "libc base addr = " << (void*) lm_libc->l_addr << std::endl;
    std::cout << "libc tls_get_addr reallocation = " << *((void**)(lm_libc->l_addr + 0x3eb058)) << std::endl;

    std::cout << "\nSpied namespace :"<< std::endl;

    handle_ld_linux = dlmopen(1, "ld-linux-x86-64.so.2", RTLD_LAZY | RTLD_NOLOAD);
    if(handle_ld_linux == nullptr){
        std::cerr << __FUNCTION__ << " : dlmopen 1 failed : "<< dlerror() << std::endl;
        return;
    }

    handle_libc = dlmopen(1, "libc.so.6", RTLD_LAZY | RTLD_NOLOAD);
    if(handle_libc == nullptr){
        std::cerr << __FUNCTION__ << " : dlmopen 0 failed : "<< dlerror() << std::endl;
        return;
    }

    if(dlinfo(handle_ld_linux, RTLD_DI_LINKMAP, &lm_ld_linux) == -1){
        std::cerr << __FUNCTION__ << " : dlinfo 1 failed : "<< dlerror() << std::endl;
        return;
    };

    if(dlinfo(handle_libc, RTLD_DI_LINKMAP, &lm_libc) == -1){
        std::cerr << __FUNCTION__ << " : dlinfo 0 failed : "<< dlerror() << std::endl;
        return;
    };

    tls_get_addr = dlsym(handle_ld_linux, "__tls_get_addr");

    std::cout << "Base addr = " << (void*) lm_ld_linux->l_addr << std::endl;
    std::cout << "tls_get_addr = "<< tls_get_addr << std::endl;
    std::cout << "libc base addr = " << (void*) lm_libc->l_addr << std::endl;
    std::cout << "libc tls_get_addr reallocation = " << *((void**)(lm_libc->l_addr + 0x3eb058)) << std::endl;
    std::cout << "\n";
}

int main(){

    try {

        SpiedProgram sp("TLSProgram");
        sp.setThreadCreationCallback([](SpiedThread& sp){
            sp.resume();
        });
        sp.start();
        sleep(1);
        sp.resume();
        sleep(3);

        dump_info();

    }catch(const std::invalid_argument& e){
        std::cerr << "SpiedProgram failed : " << e.what() << std::endl;
        std::exit(1);
    }


    return 0;
}