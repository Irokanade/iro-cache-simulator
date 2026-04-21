# iro-cache-simulator
A cache simulator of intel i7 Intel Haswell CPU (2013) demonstrating how memory access patterns affect cache performance.


## CPU Design
- **2 cores**, each with independent L1 data and instruction caches
- **L1**: 32KB, 8-way set associative, 64 sets
- **L2**: 4MB shared, 16-way set associative, 4096 sets
- **Cache line**: 64 bytes
- **Coherence**: MESI protocol with snooping
- **Replacement**: Pseudo-LRU

## Demo
Compares row-major vs column-major traversal of a 4096x4096 matrix:

```
row-major: L1D misses: 262144   L2 misses: 262144
col-major: L1D misses: 16777216 L2 misses: 262144
```

Row-major access is sequential in memory — each cache line serves 64 consecutive reads. Column-major jumps 4096 bytes per access, evicting the line before the next 63 elements are read.

## Requirements
- C++20
- g++-15

## Build
```
make
./main
```

## References
https://www.cs.cmu.edu/afs/cs/academic/class/15418-s19/www/lectures/12_snoopimpl.pdf
https://www.cs.cmu.edu/afs/cs/academic/class/15418-f24/www/lectures/10_cachecoherence.pdf
