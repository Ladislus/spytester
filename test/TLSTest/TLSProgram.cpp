#include <iostream>
#include <thread>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <dlfcn.h>
#include "TLSTestedLib.h"

static void dump_tls_info(){
    void* handle_libc = dlmopen(0, "libc.so.6", RTLD_LAZY | RTLD_NOLOAD);

    if(handle_libc == nullptr){
        std::cerr << __FUNCTION__ << " : dlmopen 0 failed : "<< dlerror() << std::endl;
        return;
    }

    void* tls_block;

    if(dlinfo(handle_libc, RTLD_DI_TLS_DATA, &tls_block) == -1){
        std::cerr << __FUNCTION__ << " : dlinfo 0 failed : "<< dlerror() << std::endl;
        return;
    };

    std::cout << "libc tls_block = "<< tls_block << std::endl;

    void* get_nprocs_addr = dlsym(handle_libc, "get_nprocs");
    if( get_nprocs_addr == nullptr ){
        std::cerr << __FUNCTION__ << " : dsym get_nprocs failed : "<< dlerror() << std::endl;
        return;
    }

    ((int(*)())get_nprocs_addr)();

}

int main(int argc, char const *argv[])
{
    update(37, 1);
    print(37);


    dump_tls_info();

    std::thread t1([]{
        update(37, 2);
        print(37);
        dump_tls_info();
    });
    t1.join();

    print(37);

    return 0;
}
