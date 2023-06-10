#include <pthread.h>

#include <iostream>
#include <unistd.h>
#include "TestLib.h"

#include <sys/sysinfo.h>

void TestFunction()
{
    std::cout << __FUNCTION__ <<" called! and b = "<< b << std::endl;
}

extern "C"
{
    int TestFunction2()
    {
        std::cout << __FUNCTION__ << " = "<< (void*)TestFunction2 << std::endl;
        return 17;
    }
}

void* TestPthreadFunction(void* arg)
{
    int tmp = 0;

    //std::cout << *((int*)arg) << std::endl;

    while(1)
    {
        tmp = testLibFunction(tmp);
        sleep(2);
    }

    return nullptr;
}

int main(int argc, char* argv[], char* envp[])
{
    std::cout << argv[0] << " ("<< getpid() <<") : started!" << std::endl;
    std::cout << "parameters("<<argc<<") = ";

    for(int i = 0; i < argc; i++ ){
        std::cout << argv[i] << " ; ";
    }
    std::cout << std::endl;

    TestFunction2();
    (void)TestLibFunction2();

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&thread, &attr, TestPthreadFunction, nullptr);

    (void)testLibFunction(100);

    pthread_detach(thread);

    std::cout << argv[0] <<" waits for child to exit!" << std::endl;
    //pthread_join(thread, nullptr);

    sleep(30);
    std::cout << "child exits" << std::endl;

    pthread_attr_destroy(&attr);

    return 0;
}