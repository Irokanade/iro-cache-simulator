#ifndef CPU_H
#define CPU_H

#include <cstdint>

constexpr uint8_t LINE_SIZE = 64;
constexpr uint8_t LINE_OFFSET_MASK = LINE_SIZE - 1;

constexpr uint16_t l1_to_index(uint64_t address)
{
    return (address >> 6) & 0x3F;
}

constexpr uint64_t l1_to_tag(uint64_t address) { return address >> 12; }

constexpr uint8_t l1_to_offset(uint64_t address)
{
    return address & LINE_OFFSET_MASK;
}

constexpr uint16_t l2_to_index(uint64_t address)
{
    return (address >> 6) & 0xFFF;
}

constexpr uint64_t l2_to_tag(uint64_t address) { return address >> 18; }

constexpr uint64_t to_line_base(uint64_t address)
{
    return address & ~static_cast<uint64_t>(LINE_OFFSET_MASK);
}

constexpr uint64_t l1_to_addr(uint64_t tag, uint16_t index)
{
    return (tag << 12) | (static_cast<uint64_t>(index) << 6);
}

constexpr uint64_t l2_to_addr(uint64_t tag, uint16_t index)
{
    return (tag << 18) | (static_cast<uint64_t>(index) << 6);
}

enum MESIState : uint8_t {
    INVALID = 0,
    MODIFIED = 1,
    EXCLUSIVE = 2,
    SHARED = 3
};

constexpr uint8_t NUM_L1_WAYS = 8;
struct L1SetMeta {
    uint64_t tag[NUM_L1_WAYS];
    MESIState state[NUM_L1_WAYS];
    uint8_t plru_bits;
};

struct L1SetData {
    uint8_t data[NUM_L1_WAYS][LINE_SIZE];
};

constexpr uint8_t NUM_L2_WAYS = 16;
struct L2SetMeta {
    uint64_t tag[NUM_L2_WAYS];
    MESIState state[NUM_L2_WAYS];
    uint8_t core_valid_d[NUM_L2_WAYS];
    uint8_t core_valid_i[NUM_L2_WAYS];
    uint16_t plru_bits;
};

struct L2SetData {
    uint8_t data[NUM_L2_WAYS][LINE_SIZE];
};

struct PerfCounters {
    uint64_t l1d_misses;
    uint64_t l1i_misses;
    uint64_t l2_misses;
};

constexpr uint8_t L1_SETS = 64;
constexpr uint8_t L1_DTLB_SETS = 4;
constexpr uint8_t L2_DTLB_SETS = 64;
constexpr uint8_t ITLB_SETS = 32;
struct Core {
    L1SetMeta l1d_metas[L1_SETS];
    L1SetData l1d_datas[L1_SETS];
    L1SetMeta l1i_metas[L1_SETS];
    L1SetData l1i_datas[L1_SETS];
    PerfCounters perf_counters;
    uint8_t core_id;
};

constexpr uint8_t NUM_CORES = 2;
constexpr uint16_t L2_SETS = 4096;
struct CPU {
    Core cores[NUM_CORES];
    L2SetMeta l2_metas[L2_SETS];
    L2SetData l2_datas[L2_SETS];
};

void cpu_read(CPU *cpu, uint8_t core_id, uint64_t address, uint8_t *data,
              uint8_t data_size);
void cpu_write(CPU *cpu, uint8_t core_id, uint64_t address, uint8_t *data,
               uint8_t data_size);
void cpu_fetch(CPU *cpu, uint8_t core_id, uint64_t address, uint8_t *data,
               uint8_t data_size);

#endif // CPU_H