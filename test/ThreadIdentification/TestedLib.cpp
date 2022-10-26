#include <pthread.h>
#include <unistd.h>
#include <mutex>
#include <queue>
#include <semaphore.h>
#include <iostream>
#include "TestedLib.h"

static bool stop1 = false;
static bool stop2 = false;


static std::mutex mutex;
static sem_t sem;
static std::queue<std::pair<int,int>> queue;

void stop()
{
    stop1 = true;
    sem_post(&sem);
}

pthread_t thread1, thread2;

void* main1(void* param)
{

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    sem_init(&sem, 0, 0);

    pthread_create(&thread1, &attr, main3, nullptr);
    //std::cout << "CONSUMER : Producer 1 created" << std::endl;
    pthread_create(&thread2, &attr, main4, nullptr);
    //std::cout << "CONSUMER : Producer 2 created" << std::endl;

    //std::cout << "CONSUMER : start consume" << std::endl;
    while(!stop1)
    {
        sem_wait(&sem);

        mutex.lock();
        if(!queue.empty()) {
            auto p = queue.front();
            queue.pop();
            mutex.unlock();

            std::cout << "CONSUMER : message " << p.second << " from thread " << p.first << std::endl;
        }
        else{
            mutex.unlock();
        }
    }

    stop2 = true;
    std::cout << "CONSUMER : wait for producer to exit" << std::endl;
    pthread_join(thread1, nullptr);
    pthread_join(thread2, nullptr);

    sem_destroy(&sem);

    return nullptr;
}

void* main3(void* param)
{
    int msgId = 0;

//    std::cout << "PRODUCER1 : start porduce" << std::endl;

    while(!stop2)
    {
        mutex.lock();
        queue.push(std::make_pair(3, ++msgId));
        mutex.unlock();
        sem_post(&sem);
        usleep(300000);
    }
    return nullptr;
}

void* main4(void* param)
{
    int msgId = 0;

    // std::cout << "PRODUCER2 : start produce" << std::endl;

    while(!stop2)
    {
        mutex.lock();
        queue.push(std::make_pair(4, ++msgId));
        mutex.unlock();
        sem_post(&sem);
        usleep(450000);
    }
    return nullptr;
}
