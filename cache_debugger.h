#ifndef CACHE_DEBUGGER_H
#define CACHE_DEBUGGER_H

#include "cpu.h"
#include <cstdint>
#include <vector>

enum Operation : uint8_t { READ, WRITE, FETCH };

struct L1Snapshot {
    uint64_t tag[NUM_L1_WAYS];
    MESIState state[NUM_L1_WAYS];
    uint8_t plru_bits;
};

struct L2Snapshot {
    uint64_t tag[NUM_L2_WAYS];
    MESIState state[NUM_L2_WAYS];
    uint8_t core_valid_d[NUM_L2_WAYS];
    uint8_t core_valid_i[NUM_L2_WAYS];
    uint16_t plru_bits;
};

struct TraceEntry {
    Operation op;
    uint8_t core_id;
    uint64_t address;
    L1Snapshot l1[NUM_CORES];
    L2Snapshot l2;
};

struct CacheDebugger {
    CPU *cpu;
    std::vector<TraceEntry> trace;
};

void cache_debugger_init(CacheDebugger *cd, CPU *cpu);
void cache_debugger_read(CacheDebugger *cd, uint8_t core_id, uint64_t address,
                         uint8_t *data, uint8_t size);
void cache_debugger_write(CacheDebugger *cd, uint8_t core_id, uint64_t address,
                          uint8_t *data, uint8_t size);
void cache_debugger_fetch(CacheDebugger *cd, uint8_t core_id, uint64_t address,
                          uint8_t *data, uint8_t size);
void cache_debugger_dump_binary(const CacheDebugger *cd, const char *path);

#endif // CACHE_DEBUGGER_H