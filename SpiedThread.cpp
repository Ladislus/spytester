//
// Created by baptiste on 13/09/22.
//

#include "SpiedThread.h"
#include "Tracer.h"

#include <sys/ptrace.h>

#include <iostream>
#include <cstring>
#include <unistd.h>

SpiedThread::SpiedThread(Tracer& tracer, pid_t pid) : _tracer(tracer), _pid(pid), _isRunning(false),
_resumeCommand(*this, &SpiedThread::resume), _stopCommand(*this, &SpiedThread::stop) {}

bool SpiedThread::isRunning(){
    bool isRunning;

    _isRunningMutex.lock();
    isRunning = _isRunning;
    _isRunningMutex.unlock();

    return isRunning;
}

void SpiedThread::resume() {
    if(!isRunning())
    {
        if(_tracer.getPid() == getpid())
        {
            if(ptrace(PTRACE_CONT, _pid, nullptr, nullptr) == -1) {
                std::cout << __FUNCTION__ << " : PTRACE_CONT failed : "<< strerror(errno) << std::endl;
            }
        }
        else
        {
            _tracer.command(&_resumeCommand);
        }
    }
}

void SpiedThread::stop() {
    if(isRunning()) {
        if(_tracer.getPid() == getpid())
        {
            if (kill(_pid, SIGSTOP) == -1) {
                std::cerr << __FUNCTION__ << " : SIGSTOP failed for "<<_pid<<" : "<<strerror(errno)<<std::endl;
            }
        }
        else
        {
            _tracer.command(&_stopCommand);
        }
    }
}

void SpiedThread::setRunning(bool isRunning) {
    _isRunningMutex.lock();
    _isRunning = isRunning;
    _isRunningMutex.unlock();
}

pid_t SpiedThread::getPid() const {
    return _pid;
}

SpiedThread::~SpiedThread() {
    std::cout << __FUNCTION__ <<" : " << _pid << std::endl;
}

