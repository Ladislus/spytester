#ifndef SPYTESTER_SPIEDNAMESPACE_H
#define SPYTESTER_SPIEDNAMESPACE_H

#include <map>
#include "ElfBin.h"

class SpiedNamespace {
public :
    SpiedNamespace(bool isBaseNamespace);
    ~SpiedNamespace() = default;

    SpiedNamespace(const SpiedNamespace&) = delete;
    SpiedNamespace& operator=(const SpiedNamespace&) = delete;

    SpiedNamespace(SpiedNamespace&&) = default;
    SpiedNamespace& operator=(SpiedNamespace&&) = default;


    ElfBin* open(const std::string& binName);
    bool close(ElfBin& bin);

private:

    std::map<void*, ElfBin> _elfBin;
    Lmid_t _id;
};


#endif //SPYTESTER_SPIEDNAMESPACE_H
