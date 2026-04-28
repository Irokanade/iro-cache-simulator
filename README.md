# iro-cache-simulator
Cachegrind inspired user space cpu cache simulator to count cache hits and cache miss of a program. This implementation is simple and uses cpu_read and cpu_write functions directly in source code to count.

### Usage Example
```cpp
CPU *cpu = static_cast<CPU *>(std::calloc(1, sizeof(CPU)));
uint8_t memory[65536] = {}; // 64KB memory
uint8_t val;

cpu_read(cpu, 0, 0x100, &val, 1, memory);

uint8_t data = 42;
cpu_write(cpu, 1, 0x100, &data, 1, memory);
cpu_read(cpu, 0, 0x100, &val, 1, memory);

std::printf("row-major: L1D misses: %llu L2 misses: %llu L3 misses: %llu\n",
            cpu->cores[0].perf_counters.l1d_misses,
            cpu->cores[0].perf_counters.l2_misses,
            cpu->cores[0].perf_counters.l3_misses);
std::free(cpu);
```

## Requirements
- C++23
- g++-15

## Demo
I also have a main.cpp demo to compare the cache misses of row-major traversal and column-major traversal.

### Build and run the demo
```
make
./main
```

### Output
Due to cache locality, the row-major traversal will have more L1D misses than the column-major traversal.
```
row-major: L1D misses: 262144 L2 misses: 262144 L3 misses: 262144
col-major: L1D misses: 16777216 L2 misses: 16777216 L3 misses: 16777216
```

## References
- [CMU 15-418/618 Fall 2024 Lecture 10: Snooping-Based Cache Coherence](https://www.cs.cmu.edu/afs/cs/academic/class/15418-f24/www/lectures/10_cachecoherence.pdf)
- [CMU 15-418/618 Spring 2019 Lecture 12: A Basic Snooping-Based Multi-Processor Implementation](https://www.cs.cmu.edu/afs/cs/academic/class/15418-s19/www/lectures/12_snoopimpl.pdf)
- [UWaterloo CS450 MESI Cache Coherence Protocol State Table](https://student.cs.uwaterloo.ca/~cs450/w18/public/mesiHandout.pdf)
