#include "TestedLib.h"
#include <pthread.h>
#include <csignal>
#include <iostream>

void handleSignal(int sig)
{
 //   std::cout << "signal " << sig << " received" << std::endl;
    if(sig ==  SIGTERM){
        stop();
    }
}

int main(int argc, char* argv[])
{
    signal(SIGTERM, handleSignal);

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&thread, &attr, main1, nullptr);

    std::cout<<"MAIN : wait for child to exit"<<std::endl;
    pthread_join(thread, nullptr);

    return 0;
}