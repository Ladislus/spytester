#include <pthread.h>

#include <iostream>
#include <unistd.h>
#include "TestLib.h"

void TestFunction()
{
    std::cout << __FUNCTION__ <<" called!" << std::endl;
}

extern "C"
{
    int TestFunction2()
    {
        return 17;
    }
}

void* TestPthreadFunction(void* arg)
{

    if( arg!= nullptr )
    {
        return nullptr;
    }
    int tmp = 0;
    while(1)
    {
        tmp = testLibFunction(tmp);
        std::cout << __FUNCTION__ <<" : testLibFunction(tmp) = "<< tmp <<std::endl;
        sleep(2);
    }

    return nullptr;
}

int main(int argc, char* argv[], char* envp[])
{
    printf("ICI ibzefbizebfibz : %p\n", &std::cout);
    //std::cerr << "ICI ibzefbizebfibz 2" << std::endl;
    std::cout << "ICI ibzefbizebfibz 3" << std::endl;
    //std::cout << argv[0] << " ("<< getpid() <<") : started!" << std::endl;

    TestFunction();
    (void)TestFunction2();

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&thread, &attr, TestPthreadFunction, nullptr);
    std::cout << argv[0] <<" waits for child to exit!" << std::endl;

    sleep(2);

    pthread_join(thread, nullptr);

    pthread_attr_destroy(&attr);

    return 0;
}