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
        sleep(2);
    }

    return nullptr;
}

int main(int argc, char* argv[], char* envp[])
{
    std::cout << argv[0] << " ("<< getpid() <<") : started!" << std::endl;

    TestFunction();
    (void)TestFunction2();

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&thread, &attr, TestPthreadFunction, nullptr);

    (void)testLibFunction(100);

    std::cout << argv[0] <<" waits for child to exit!" << std::endl;
    pthread_join(thread, nullptr);

    std::cout << "child exits" << std::endl;

    pthread_attr_destroy(&attr);

    return 0;
}