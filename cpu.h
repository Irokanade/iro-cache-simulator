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
    return (address >> 6) & 0x1FF;
}

constexpr uint64_t l2_to_tag(uint64_t address) { return address >> 15; }

constexpr uint16_t l3_to_index(uint64_t address)
{
    return (address >> 6) & 0x1FFF;
}

constexpr uint64_t l3_to_tag(uint64_t address) { return address >> 19; }

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
    return (tag << 15) | (static_cast<uint64_t>(index) << 6);
}

constexpr uint64_t l3_to_addr(uint64_t tag, uint16_t index)
{
    return (tag << 19) | (static_cast<uint64_t>(index) << 6);
}

enum MESIState : uint8_t {
    INVALID = 0,
    MODIFIED = 1,
    EXCLUSIVE = 2,
    SHARED = 3
};

constexpr uint8_t NUM_L1_WAYS = 8;
struct alignas(64) L1SetMeta {
    uint64_t tag[NUM_L1_WAYS];
    MESIState state[NUM_L1_WAYS];
    uint8_t plru_bits;
};

struct alignas(64) L1SetData {
    uint8_t data[NUM_L1_WAYS][LINE_SIZE];
};

constexpr uint8_t NUM_L2_WAYS = 8;
struct alignas(64) L2SetMeta {
    uint64_t tag[NUM_L2_WAYS];
    MESIState state[NUM_L2_WAYS];
    uint8_t plru_bits;
};

struct alignas(64) L2SetData {
    uint8_t data[NUM_L2_WAYS][LINE_SIZE];
};

constexpr uint8_t NUM_L3_WAYS = 16;
struct alignas(64) L3SetMeta {
    uint64_t tag[NUM_L3_WAYS];
    MESIState state[NUM_L3_WAYS];
    uint8_t core_valid_d[NUM_L3_WAYS];
    uint8_t core_valid_i[NUM_L3_WAYS];
    uint16_t plru_bits;
};

struct alignas(64) L3SetData {
    uint8_t data[NUM_L3_WAYS][LINE_SIZE];
};

struct PerfCounters {
    uint64_t l1d_misses;
    uint64_t l1i_misses;
    uint64_t l2_misses;
    uint64_t l3_misses;
};

constexpr uint8_t L1_SETS = 64;
constexpr uint16_t L2_SETS = 512;
constexpr uint16_t L3_SETS = 8192;

struct Core {
    L1SetMeta l1d_metas[L1_SETS];
    L1SetData l1d_datas[L1_SETS];
    L1SetMeta l1i_metas[L1_SETS];
    L1SetData l1i_datas[L1_SETS];
    L2SetMeta l2_metas[L2_SETS];
    L2SetData l2_datas[L2_SETS];
    PerfCounters perf_counters;
};

constexpr uint8_t NUM_CORES = 4;
struct CPU {
    Core cores[NUM_CORES];
    L3SetMeta l3_metas[L3_SETS];
    L3SetData l3_datas[L3_SETS];
};

void cpu_read(CPU *cpu, uint8_t core_id, uint64_t address, uint8_t *data,
              uint8_t data_size, uint8_t *memory);
void cpu_write(CPU *cpu, uint8_t core_id, uint64_t address, uint8_t *data,
               uint8_t data_size, uint8_t *memory);
void cpu_fetch(CPU *cpu, uint8_t core_id, uint64_t address, uint8_t *data,
               uint8_t data_size, uint8_t *memory);

#endif // CPU_H
