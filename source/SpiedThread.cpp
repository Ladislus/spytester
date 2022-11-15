#include "../include/SpiedThread.h"
#include "../include/Tracer.h"
#include "../include/Breakpoint.h"
#include "../include/SpiedProgram.h"

#include <sys/ptrace.h>
#include <chrono>
#include <iostream>
#include <cstring>
#include <sys/user.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/procfs.h>

#define tkill(tid, sig) syscall(SYS_tkill, tid, sig)
#define STATE_TIMEOUT std::chrono::seconds(1)

SpiedThread::SpiedThread(SpiedProgram& spiedProgram, Tracer& tracer, pid_t tid) : _tracer(tracer), _tid(tid),
_state(STOPPED), _isSigTrapExpected(false), _spiedProgram(spiedProgram), _code(0),
_stateLock(_stateMutex, std::defer_lock)
{
    for(uint32_t idx = 0; idx<WatchPoint::maxNb; idx++){
        _watchPoints.emplace_back(std::make_pair(WatchPoint(_tracer, *this, idx), false));
    }
}

SpiedThread::~SpiedThread() {
    std::cout << __FUNCTION__ << " : " << _tid << std::endl;
}

pid_t SpiedThread::getTid() const {
    return _tid;
}

void SpiedThread::setState(E_State state, int code) {
    std::unique_lock lk(_stateMutex);
    _code = code;
    _state = state;
    lk.unlock();

    _stateCV.notify_all();
}

bool SpiedThread::resume(int signum) {

    return _tracer.command([this, signum] {
        bool success;

        if (ptrace(PTRACE_CONT, _tid, nullptr, signum) == -1) {
            success = false;
            std::cerr << "SpiedThread::resume : PTRACE_CONT failed for " << _tid << " : " << strerror(errno)
                      << std::endl;
        } else {
            success = true;
            setState(RUNNING, 0);
            std::cout << "SpiedThread::resume : thread " <<_tid <<" resumed" <<  std::endl;
        }

        return success;
    });
}

bool SpiedThread::singleStep() {
    return _tracer.command([this] {
        bool success = true;
        std::unique_lock stateLk(_stateMutex);

        _isSigTrapExpected = true;
        if (ptrace(PTRACE_SINGLESTEP, _tid, nullptr, nullptr) == -1) {
            success = false;
            _isSigTrapExpected = false;
            std::cerr << "SpiedThread::singleStep : PTRACE_SINGLESTEP failed for "
                      << _tid << " : " << strerror(errno) << std::endl;
        } else {
            _state = RUNNING;
            if (!_stateCV.wait_for(stateLk, STATE_TIMEOUT, [this] { return _state == STOPPED; })) {
                success = false;
                _isSigTrapExpected = false;
                std::cerr << "SpiedThread::singleStep timeout for thread " << _tid << std::endl;
            }
        }

        return success;
    });
}

bool SpiedThread::stop() {
    return _tracer.command([this] {
        bool success = true;
        std::unique_lock lk(_stateMutex);

        if(_state != STOPPED) {
            if (tkill(_tid, SIGSTOP) == -1) {
                success = false;
                std::cerr << "SpiedThread::stop : SIGSTOP failed for " << _tid << " : "
                          << strerror(errno) << std::endl;
            } else if (!_stateCV.wait_for(lk, STATE_TIMEOUT, [this] { return _state == STOPPED; })) {
                success = false;
                std::cerr << "SpiedThread::stop timeout for " << _tid << std::endl;
            }
        }

        return success;
    });
}

bool SpiedThread::terminate() {
    return _tracer.command([this] {
        std::unique_lock lk(_stateMutex);
        bool success = true;

        if (_state != TERMINATED) {
            if (_state != STOPPED) {
                if(tkill(_tid, SIGTERM) == -1) {
                    std::cerr << "SpiedThread::terminate : SIGTERM failed for "
                              << _tid << " : " << strerror(errno) << std::endl;
                    return false;
                } else if(!_stateCV.wait_for(lk, STATE_TIMEOUT,
                                             [this] { return _state == STOPPED;})) {
                    std::cerr << "SpiedThread::terminate STOP timeout for " << _tid << std::endl;
                    return false;
                }
            }

            if(ptrace(PTRACE_CONT, _tid, nullptr ,SIGTERM) == -1) {
                std::cerr << "SpiedThread::terminate : PTRACE_CONT failed for "
                          << _tid << " : " << strerror(errno) << std::endl;
                return false;
            } else if(!_stateCV.wait_for(lk, STATE_TIMEOUT,
                                         [this] { return _state == TERMINATED;})) {
                std::cerr << "SpiedThread::terminate TERMINATE timeout for " << _tid << std::endl;
                return false;
            }
        }

        return success;
    });
}

bool SpiedThread::handleSigTrap(int wstatus) {
    if(!_isSigTrapExpected) {
        return _tracer.command([this, wstatus] {
            struct user_regs_struct regs;


            // Handle thread creation
            if ((wstatus >> 16) == PTRACE_EVENT_CLONE) {

                unsigned long newThread;
                ptrace(PTRACE_GETEVENTMSG, _tid, nullptr, &newThread);

                std::cout << "SpiedThread::handleSigTrap : thread " << _tid << " create new thread "
                          << newThread << std::endl;

                resume();
                return true;
            }

            // Handle breakpoint
            if (ptrace(PTRACE_GETREGS, _tid, NULL, &regs) == -1) {
                std::cerr << "SpiedThread::handleSigTrap : PTRACE_GETREGS failed : " << strerror(errno) << std::endl;
                return true;
            }

            BreakPoint *const bp = _spiedProgram.getBreakPointAt((void *) (regs.rip - 1));
            if (bp != nullptr) {
                bp->hit(*this);
                return true;
            }

            // Handle watchpoint
            uint64_t dr6 = ptrace(PTRACE_PEEKUSER, _tid, offsetof(struct user, u_debugreg[6]), NULL);
            if (dr6 == -1) {
                std::cerr << "SpiedThread::handleSigTrap : PTRACE_PEEKUSER failed for thread << "
                          << _tid << " : " << strerror(errno) << std::endl;
                return true;
            }

            uint32_t idx;
            for (idx = 0; idx < WatchPoint::maxNb; idx++) {
                if (dr6 & (1 << idx)) break;
            }

            if (idx < _watchPoints.size()) {
                // Clean dr6 register
                dr6 &= ~(1 << idx);

                if (ptrace(PTRACE_POKEUSER, _tid, offsetof(struct user, u_debugreg[6]), dr6) == -1) {
                    std::cerr << "SpiedThread::handleSigTrap : PTRACE_POKEUSER failed for thread << "
                              << _tid << " : " << strerror(errno) << std::endl;
                }

                WatchPoint &watchPoint = _watchPoints[idx].first;
                watchPoint.hit();

                return true;
            }

            // Handle unexpected SIGTRAP
            Dl_info info;
            if (dladdr((void *) (regs.rip - 1), &info) == 0) {
                std::cerr << "SpiedThread::handleSigTrap : unexpected SIGTRAP for thread " << _tid << std::endl;
            } else {
                std::cout << "SpiedThread::handleSigTrap : SIGTRAP at " << info.dli_sname << " (" << info.dli_fname
                          << ") for " << _tid << std::endl;
            }
            resume();

            return false;
        }, false);
    } else {
        _isSigTrapExpected = false;
    }

    return true;
}

WatchPoint *SpiedThread::createWatchPoint() {
    WatchPoint* watchPoint = nullptr;

    for(auto& wp : _watchPoints){
        if(!wp.second){
            watchPoint = &wp.first;
            break;
        }
    }

    return watchPoint;
}

void SpiedThread::deleteWatchPoint(WatchPoint *watchPoint) {
    for (auto & wp : _watchPoints){
        if(watchPoint == &wp.first){
            watchPoint->unset();
            wp.second = false;
            break;
        }
    }
}

bool SpiedThread::backtrace() {
    return _tracer.command([this] {
        // Get register
        struct user_regs_struct regs{};
        if (ptrace(PTRACE_GETREGS, _tid, NULL, &regs) == -1) {
            std::cerr << "SpiedThread::backtrace : PTRACE_GETREGS failed for thread "
                      << _tid << std::endl;
            return false;
        }

        // Print thread current position
        Dl_info info;
        if (dladdr((void *) regs.rip, &info) == 0) {
            std::cout << "SpiedThread::backtrace : thread " << _tid << " at " << (void *) regs.rip << std::endl;
        } else {
            if (info.dli_sname != nullptr) {
                std::cout << "SpiedThread::backtrace : thread " << _tid << " at " << info.dli_sname << " ("
                          << info.dli_fname << ")" << std::endl;
            } else {
                std::cout << "thread " << _tid << " at " << (void *) regs.rip
                          << " (" << info.dli_fname << ")" << std::endl;
                return true;
            }
        }

        // Print call stack
        auto rbp = (uint64_t *) regs.rbp;
        while (rbp != nullptr) {
            uint64_t retAddr = *(rbp + 1);
            rbp = (uint64_t *) (*rbp);

            if (dladdr((void *) retAddr, &info) == 0) {
                break;
            } else {
                if (info.dli_sname != nullptr) {
                    std::cout << "\tfrom " << info.dli_sname << " (" << info.dli_fname << ")" << std::endl;
                } else {
                    std::cout << "\tfrom ?? (" << (void *) retAddr << ") (" << info.dli_fname << ")" << std::endl;
                }
            }
        }
        return true;
    }, false);
}

bool SpiedThread::detach() {
    return _tracer.command([this] {
        bool res = true;
        if (ptrace(PTRACE_DETACH, _tid, 0, 0) == -1) {
            res = false;
            std::cerr << "SpiedThread::detach : PTRACE_DETACH failed for thread " << _tid << " : "
                      << strerror(errno) << std::endl;
        }
        return res;
    });
}

