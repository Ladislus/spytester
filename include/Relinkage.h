#ifndef SPYTESTER_RELINKAGE_H
#define SPYTESTER_RELINKAGE_H

#include <vector>
#include <cstdint>

class DynamicModule;

class Relinkage {
public:
    Relinkage(DynamicModule& source, DynamicModule& destination);
    ~Relinkage();

    Relinkage(const Relinkage& other) = delete;
    Relinkage& operator=(const Relinkage& other) = delete;

    Relinkage(Relinkage&& other) = delete;
    Relinkage& operator=(Relinkage&& other) = delete ;

    void invalidate();

private:
    bool _validity;
    std::vector<std::pair<uint64_t*, uint64_t>> _previousRelocations;
    DynamicModule& _source;
    DynamicModule& _destination;
};


#endif //SPYTESTER_RELINKAGE_H
