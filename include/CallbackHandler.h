#ifndef SPYTESTER_CALLBACKHANDLER_H
#define SPYTESTER_CALLBACKHANDLER_H

#include <functional>
#include <thread>
#include <queue>
#include <mutex>
#include <semaphore.h>

class CallbackHandler {
public :
    CallbackHandler();
    ~CallbackHandler();
    void executeCallback(const std::function<void()>& callback);

private :
    bool _running;
    std::thread _callbackHandler;
    std::queue<std::function<void()>> _callbacks;
    std::mutex _callbackMutex;
    sem_t _callbackSem;

    void handleCallback();

};


#endif //SPYTESTER_CALLBACKHANDLER_H
