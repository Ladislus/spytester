
#include "../include/CallbackHandler.h"
#include <iostream>
#include <cstring>

CallbackHandler::CallbackHandler() : _running(true) {
    if(sem_init(&_callbackSem, 0, 0) == -1){
        std::cerr << __FUNCTION__ << " : semaphore initialization failed : " << strerror(errno) << std::endl;
        throw std::invalid_argument("invalid sem init");
    }

    _callbackHandler = std::thread(&CallbackHandler::handleCallback, this);
}

CallbackHandler::~CallbackHandler() {
    _running = false;
    sem_post(&_callbackSem);

    _callbackHandler.join();

    sem_destroy(&_callbackSem);
}

void CallbackHandler::executeCallback(const std::function<void()> &callback) {
    _callbackMutex.lock();
    _callbacks.push(callback);
    _callbackMutex.unlock();

    sem_post(&_callbackSem);
}

void CallbackHandler::handleCallback() {
    while(sem_wait(&_callbackSem) == 0){

        _callbackMutex.lock();
        while(!_callbacks.empty()){
            auto callback = std::move(_callbacks.front());
            _callbacks.pop();
            _callbackMutex.unlock();

            callback();

            _callbackMutex.lock();
        }
        _callbackMutex.unlock();

        if(!_running) break;
    }
}
