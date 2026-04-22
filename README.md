# iro-cache-simulator
A three-level cache simulator based on the Intel Core i7 architecture as presented in CMU 15-418/618 (Parallel Computer Architecture and Programming).

## CPU Design
- **4 cores**, each with independent L1 data and instruction caches
- **L1**: 32KB, 8-way set associative, 64 sets (private per core)
- **L2**: 256KB, 8-way set associative, 512 sets (private per core)
- **L3**: 8MB, 16-way set associative, 8192 sets (shared, inclusive)
- **Cache line**: 64 bytes
- **Coherence**: MESI protocol with snooping at L3
- **Replacement**: Pseudo-LRU (tree-based)
- **Write policy**: Write-back, write-allocate

## Documentation

### Setup

Allocate a `CPU` struct with `calloc` (all caches start cold/invalid) and a memory buffer:

```cpp
CPU *cpu = (CPU *)calloc(1, sizeof(CPU));
uint8_t memory[MEMORY_SIZE] = {};
```

### Functions

#### `cpu_read`
```cpp
void cpu_read(CPU *cpu, uint8_t core_id, uint64_t address,
              uint8_t *data, uint8_t data_size, uint8_t *memory);
```
Simulate a data load. Reads `data_size` bytes starting at `address` into `data`.

| Parameter | Description |
|---|---|
| `cpu` | Pointer to the CPU state |
| `core_id` | Core performing the read (0-3) |
| `address` | Memory address to read from |
| `data` | Destination buffer for the read data |
| `data_size` | Number of bytes to read (must not cross a cache line boundary) |
| `memory` | Pointer to main memory backing store |

#### `cpu_write`
```cpp
void cpu_write(CPU *cpu, uint8_t core_id, uint64_t address,
               uint8_t *data, uint8_t data_size, uint8_t *memory);
```
Simulate a data store. Writes `data_size` bytes from `data` to `address`.

Parameters are the same as `cpu_read`, except `data` is the source buffer containing bytes to write.

#### `cpu_fetch`
```cpp
void cpu_fetch(CPU *cpu, uint8_t core_id, uint64_t address,
               uint8_t *data, uint8_t data_size, uint8_t *memory);
```
Simulate an instruction fetch. Same as `cpu_read` but uses the L1 instruction cache instead of the L1 data cache. Instruction lines are always filled as Shared (read-only).

### Performance Counters

Each core tracks cache miss counts:

```cpp
cpu->cores[core_id].perf_counters.l1d_misses;  // L1 data cache misses
cpu->cores[core_id].perf_counters.l1i_misses;  // L1 instruction cache misses
cpu->cores[core_id].perf_counters.l2_misses;   // L2 cache misses
cpu->cores[core_id].perf_counters.l3_misses;   // L3 cache misses
```

### Example

```cpp
CPU *cpu = (CPU *)calloc(1, sizeof(CPU));
uint8_t memory[65536] = {};
uint8_t val;

// Core 0 reads address 0x100
cpu_read(cpu, 0, 0x100, &val, 1, memory);

// Core 1 writes 42 to address 0x100
uint8_t data = 42;
cpu_write(cpu, 1, 0x100, &data, 1, memory);

// Core 0 reads again (gets updated value via MESI coherence)
cpu_read(cpu, 0, 0x100, &val, 1, memory);
// val == 42

printf("L1D misses: %llu\n", cpu->cores[0].perf_counters.l1d_misses);
free(cpu);
```

## Requirements
- C++23
- g++-15

## Build
```
make
./main
```

## References
- [CMU 15-418/618 Fall 2024 - Lecture 10: Snooping-Based Cache Coherence](https://www.cs.cmu.edu/afs/cs/academic/class/15418-f24/www/lectures/10_cachecoherence.pdf)
- [CMU 15-418/618 Spring 2019 - Lecture 12: A Basic Snooping-Based Multi-Processor Implementation](https://www.cs.cmu.edu/afs/cs/academic/class/15418-s19/www/lectures/12_snoopimpl.pdf)
- [UWaterloo CS450 - MESI Cache Coherence Protocol State Table](https://student.cs.uwaterloo.ca/~cs450/w18/public/mesiHandout.pdf)
