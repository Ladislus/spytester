#ifndef SPYTESTER_DYNAMICLINKER_H
#define SPYTESTER_DYNAMICLINKER_H

#include <elf.h>
#include <vector>

size_t getDynEntry(struct link_map* lm, Elf64_Sxword tag, std::vector<uint64_t>& entriesFound);
void* getDefinition(void* handle, const char* symName);

#endif //SPYTESTER_DYNAMICLINKER_H