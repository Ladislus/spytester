#include "SpiedThread.h"
#include "Tracer.h"

#include <sys/ptrace.h>

#include <iostream>
#include <cstring>

SpiedThread::SpiedThread(Tracer& tracer, pid_t tid) : _tracer(tracer), _tid(tid), _isRunning(false)
{}

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
        if(_tracer.isTracerThread())
        {
            if(ptrace(PTRACE_CONT, _tid, nullptr, nullptr) == -1) {
                std::cerr << __FUNCTION__ << " : PTRACE_CONT failed for " << _tid << " : " << strerror(errno) << std::endl;
            }
        }
        else
        {
            _tracer.command(std::make_unique<SpiedThreadCmd>(*this, &SpiedThread::resume));
        }
    }
}

void SpiedThread::stop() {
    if(isRunning()) {
        if(_tracer.isTracerThread())
        {
            if (kill(_tid, SIGSTOP) == -1) {
                std::cerr << __FUNCTION__ << " : SIGSTOP failed for " << _tid << " : " << strerror(errno) << std::endl;
            }
        }
        else
        {
            _tracer.command(std::make_unique<SpiedThreadCmd>(*this, &SpiedThread::stop));
        }
    }
}

void SpiedThread::setRunning(bool isRunning) {
    _isRunningMutex.lock();
    _isRunning = isRunning;
    _isRunningMutex.unlock();
}

pid_t SpiedThread::getTid() const {
    return _tid;
}

SpiedThread::~SpiedThread() {
    std::cout << __FUNCTION__ << " : " << _tid << std::endl;
}

