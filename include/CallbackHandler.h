#ifndef SPYTESTER_CALLBACKHANDLER_H
#define SPYTESTER_CALLBACKHANDLER_H


#include <functional>
#include <mutex>
#include <queue>
#include <thread>

#include <semaphore.h>

#include "Logger.h"

class CallbackHandler {
private :

    // Local aliases to reduce typing/increase meaning
    using Callback = std::function<void()>;
    using CallbackQueue = std::queue<Callback>;
    using Semaphore = sem_t;

    // Attributs
    bool _running;
    std::thread _callbackHandler;
    CallbackQueue _callbacks;
    std::mutex _callbackMutex;
    Semaphore _callbackSem;

    // Functions
    void handleCallback();

public :
    // Public interface
    CallbackHandler();
    ~CallbackHandler();
    void executeCallback(const Callback& callback);
};


#endif //SPYTESTER_CALLBACKHANDLER_H
