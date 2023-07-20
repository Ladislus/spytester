#include <cstring>
#include <iostream>

#include "CallbackHandler.h"

CallbackHandler::CallbackHandler(): _running(true) {
    if(sem_init(&this->_callbackSem, 0, 0) == -1){
        error_log("Semaphore initialization failed (" << strerror(errno) << ")");
        throw std::invalid_argument("Invalid sem init");
    }

    this->_callbackHandler = std::thread(&CallbackHandler::handleCallback, this);
}

CallbackHandler::~CallbackHandler() {
    this->_running = false;
    if (sem_post(&this->_callbackSem) == -1)
        error_log("Semaphore post failed (" << strerror(errno) << ")");

    this->_callbackHandler.join();

    if (sem_destroy(&this->_callbackSem) == -1)
        error_log("Semaphore destruction failed (" << strerror(errno) << ")");
}

void CallbackHandler::executeCallback(const Callback& callback) {
    this->_callbackMutex.lock();
    this->_callbacks.push(callback);
    this->_callbackMutex.unlock();

    if (sem_post(&this->_callbackSem) == -1)
        error_log("Semaphore post failed (" << strerror(errno) << ")");
}

void CallbackHandler::handleCallback() {
    while(sem_wait(&this->_callbackSem) == 0){

        this->_callbackMutex.lock();
        while(!this->_callbacks.empty()){
            auto callback = std::move(this->_callbacks.front());
            this->_callbacks.pop();
            this->_callbackMutex.unlock();

            callback();

            this->_callbackMutex.lock();
        }
        this->_callbackMutex.unlock();

        if(!this->_running) break;
    }
}
