#include "cpu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

constexpr int N = 4096;
static uint8_t matrix[N][N];

static void row_major_order(CPU *cpu)
{
    uint8_t tmp;
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            cpu_read(cpu, 0, reinterpret_cast<uint64_t>(&matrix[i][j]), &tmp,
                     sizeof(uint8_t));
        }
    }
}

static void col_major_order(CPU *cpu)
{
    uint8_t tmp;
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            cpu_read(cpu, 0, reinterpret_cast<uint64_t>(&matrix[i][j]), &tmp,
                     sizeof(uint8_t));
        }
    }
}

int main()
{
    CPU *cpu = static_cast<CPU *>(std::calloc(1, sizeof(CPU)));

    row_major_order(cpu);
    std::printf("row-major: L1D misses: %llu L2 misses: %llu\n",
                cpu->cores[0].perf_counters.l1d_misses,
                cpu->cores[0].perf_counters.l2_misses);

    std::memset(cpu, 0, sizeof(CPU));
    col_major_order(cpu);
    std::printf("col-major: L1D misses: %llu L2 misses: %llu\n",
                cpu->cores[0].perf_counters.l1d_misses,
                cpu->cores[0].perf_counters.l2_misses);

    std::free(cpu);
    return 0;
}
