#include "cache_debugger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

constexpr int N = 4096;
static uint8_t matrix[N][N];

static void row_major_order(CacheDebugger *cd)
{
    uint8_t tmp;
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            cache_debugger_read(cd, 0,
                                reinterpret_cast<uint64_t>(&matrix[i][j]),
                                &tmp, sizeof(uint8_t));
        }
    }
}

static void col_major_order(CacheDebugger *cd)
{
    uint8_t tmp;
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            cache_debugger_read(cd, 0,
                                reinterpret_cast<uint64_t>(&matrix[i][j]),
                                &tmp, sizeof(uint8_t));
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

    CacheDebugger cd;
    cache_debugger_init(&cd, cpu);

    row_major_order(&cd);
    std::printf("row-major: L1D misses: %llu L2 misses: %llu\n",
                cpu->cores[0].perf_counters.l1d_misses,
                cpu->cores[0].perf_counters.l2_misses);
    cache_debugger_dump_binary(&cd, "row_major.trace");

    std::memset(cpu, 0, sizeof(CPU));
    cache_debugger_init(&cd, cpu);

    col_major_order(&cd);
    std::printf("col-major: L1D misses: %llu L2 misses: %llu\n",
                cpu->cores[0].perf_counters.l1d_misses,
                cpu->cores[0].perf_counters.l2_misses);
    cache_debugger_dump_binary(&cd, "col_major.trace");

    std::free(cpu);
    return 0;
}
