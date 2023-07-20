#ifndef SPYTESTER_LOGGER_H
#define SPYTESTER_LOGGER_H

#include <iostream>
#include <sstream>

[[noreturn]]
void _fatal(const std::string& msg, const char* file = __builtin_FILE(), const char* function = __builtin_FUNCTION(), int line = __builtin_LINE());
void _error(const std::string& msg, const char* file = __builtin_FILE(), const char* function = __builtin_FUNCTION(), int line = __builtin_LINE());
void _info(const std::string& msg, const char* file = __builtin_FILE(), const char* function = __builtin_FUNCTION(), int line = __builtin_LINE());

#define fatal_log(_msg)             \
    {                               \
        std::stringstream ss;       \
        ss << _msg;                 \
        _fatal(ss.str());           \
    }

#define error_log(_msg)             \
    {                               \
        std::stringstream ss;       \
        ss << _msg;                 \
        _error(ss.str());           \
    }

#define info_log(_msg)              \
    {                               \
        std::stringstream ss;       \
        ss << _msg;                 \
        _info(ss.str());            \
    }

#endif //SPYTESTER_LOGGER_H