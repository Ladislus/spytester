#include <pthread.h>

#include <iostream>
#include <unistd.h>

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
    std::cout << __FUNCTION__ << " : hola!" << std::endl;

    return nullptr;
}

int main(int argc, char* argv[], char* envp[])
{
    std::cout << argv[0] <<" started!" << std::endl;

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