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
