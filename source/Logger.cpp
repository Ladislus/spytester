#include "Logger.h"

std::string _prefix(const char* file, const char* function, int line) {
    std::string filepath = file;

    const auto idx = filepath.find_last_of("/");
    if (idx == std::string::npos)
        return filepath + ":" + std::to_string(line) + " [" + function + "]";
    else
        return filepath.substr(idx + 1) + ":" + std::to_string(line) + " [" + function + "]";
}

[[noreturn]]
void _fatal(const std::string& msg, const char* file, const char* function, int line) {
    std::cerr << _prefix(file, function, line) << " FATAL: " << msg << std::endl;
    std::abort();
}

void _error(const std::string& msg, const char* file, const char* function, int line) {
    std::cerr << _prefix(file, function, line) << " ERROR: " << msg << std::endl;
}

void _info(const std::string& msg, const char* file, const char* function, int line) {
    std::cout << _prefix(file, function, line) << ": " << msg << std::endl;
}
