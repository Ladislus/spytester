#include <iostream>
#include "../../include/SpyLoader.h"
#include <thread>
#include <future>

int main(){
    std::cout << "HOLA AMIGO : " << &getSpyLoader() << std::endl;

    std::promise<void(*)(int)> p;
    auto f = p.get_future();

    std::thread t1([f = std::move(f)]() mutable {
        auto print = f.get();
        print(6);
    });

    void* handle = dlmopen(3, "libTLSTestedLib.so", RTLD_LAZY);
    if(handle == nullptr){
        std::cerr << __FUNCTION__ << " failed to load libTLSTestedLib.so : " << dlerror() << std::endl;
        return 1;
    }

    auto print = (void(*)(int)) dlsym(handle, "print");
    if(print == nullptr){
        std::cerr << __FUNCTION__ << " failed to find print : " << dlerror() << std::endl;
        return 1;
    }

    p.set_value(print);

    t1.join();

    pthread_key_t key;

    std::thread t2([&key, print]{
        std::cout << "HOLA THREADER" << std::endl;
        pthread_key_create(&key, nullptr);
        print(4);
    });
    t2.join();

    pthread_key_delete(key);

    return 0;
}