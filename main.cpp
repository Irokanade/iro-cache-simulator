#include "cpu.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

constexpr int ROWS = 4096;
constexpr int COLS = 4096;
constexpr uint64_t MEMORY_SIZE = static_cast<uint64_t>(ROWS) * COLS;
static uint8_t memory[MEMORY_SIZE];

static void row_major_order(CPU *cpu)
{
    uint8_t tmp;
    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < COLS; j++) {
            uint64_t addr = static_cast<uint64_t>(i * COLS + j);
            cpu_read(cpu, 0, addr, &tmp, sizeof(uint8_t), memory);
        }
    }
}

static void col_major_order(CPU *cpu)
{
    uint8_t tmp;
    for (int j = 0; j < COLS; j++) {
        for (int i = 0; i < ROWS; i++) {
            uint64_t addr = static_cast<uint64_t>(i * COLS + j);
            cpu_read(cpu, 0, addr, &tmp, sizeof(uint8_t), memory);
        }
    }
}

int main()
{
    CPU *cpu = static_cast<CPU *>(std::calloc(1, sizeof(CPU)));
    if (!cpu) {
        std::fprintf(stderr, "Failed to allocate CPU\n");
        return 1;
    }

    row_major_order(cpu);
    std::printf("row-major: L1D misses: %llu L2 misses: %llu L3 misses: %llu\n",
                cpu->cores[0].perf_counters.l1d_misses,
                cpu->cores[0].perf_counters.l2_misses,
                cpu->cores[0].perf_counters.l3_misses);

    std::memset(cpu, 0, sizeof(CPU));
    col_major_order(cpu);
    std::printf("col-major: L1D misses: %llu L2 misses: %llu L3 misses: %llu\n",
                cpu->cores[0].perf_counters.l1d_misses,
                cpu->cores[0].perf_counters.l2_misses,
                cpu->cores[0].perf_counters.l3_misses);

    std::free(cpu);
    return 0;
}
