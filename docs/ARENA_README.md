# DB25 Arena Allocator - Technical Documentation

## Overview

The DB25 Arena Allocator is a high-performance memory management system designed specifically for AST construction in SQL parsing. It achieves sub-10 nanosecond allocation times through bump-pointer allocation.

## Key Features

- **Ultra-Fast Allocation**: 6.5ns average allocation time
- **Zero Fragmentation**: Bump-pointer allocation eliminates gaps
- **Cache-Line Aligned**: 64-byte alignment for optimal CPU cache usage
- **Thread-Safe**: Thread-local arenas for zero-contention parallel parsing
- **Bulk Deallocation**: O(1) reset/clear operations
- **Geometric Growth**: Blocks grow from 64KB to 1MB maximum

## Academic Paper

A comprehensive academic paper describing the design and implementation is available:

- **PDF**: [`arena_allocator_paper.pdf`](arena_allocator_paper.pdf)
- **Source**: [`arena_allocator_paper.tex`](arena_allocator_paper.tex)

### Paper Contents

1. **Architecture**: Core concepts, block management, allocation algorithms
2. **Performance Analysis**: Time/space complexity, benchmarks
3. **Implementation Details**: Alignment handling, thread-local storage
4. **Testing**: 67 test cases with 98.5% pass rate
5. **Comparisons**: Performance vs malloc and other allocators

### Building the Paper

```bash
cd docs
make         # Compile to PDF
make view    # Compile and open
make clean   # Remove generated files
```

## Quick Start

```cpp
#include "db25/memory/arena.hpp"

// Basic usage
db25::Arena arena;
void* ptr = arena.allocate(size, alignment);

// Type-safe allocation
auto* node = arena.allocate_object<ASTNode>(args...);

// Thread-local arena
auto& tl_arena = db25::ThreadLocalArena::get();

// Bulk operations
arena.reset();  // Keep memory, reset usage
arena.clear();  // Release all memory
```

## Performance Metrics

| Metric | Target | Achieved |
|--------|--------|----------|
| Single allocation | < 5 ns | 6.5 ns |
| Throughput | > 100M ops/s | 152M ops/s |
| 1M allocations | < 10 ms | 6 ms |
| Memory efficiency | > 90% | 87-99% |
| Reset time | < 100 μs | < 1 μs |

## Design Principles

### Bump-Pointer Allocation
```
Before: [####____] used=4, size=8
Allocate(2): ptr = data + used; used += 2
After:  [######__] used=6, size=8
```

### Geometric Block Growth
```
Block 0: 64KB
Block 1: 128KB  
Block 2: 256KB
Block 3: 512KB
Block 4: 1MB (max)
```

### Cache-Line Alignment
```
Node 0: |--------64 bytes--------|
Node 1: |--------64 bytes--------|  
Node 2: |--------64 bytes--------|
        ↑ No false sharing between nodes
```

## Testing

Run comprehensive test suite:

```bash
cd build
./tests/arena/test_arena_basic   # 26 basic tests
./tests/arena/test_arena_stress  # 13 stress tests
./tests/arena/test_arena_edge    # 28 edge cases
```

## Implementation Files

- **Header**: [`include/db25/memory/arena.hpp`](../include/db25/memory/arena.hpp)
- **Source**: [`src/memory/arena.cpp`](../src/memory/arena.cpp)
- **Tests**: [`tests/arena/`](../tests/arena/)

## Use Cases

Ideal for:
- Parser AST construction
- Temporary computation buffers
- Batch processing with clear phases
- Per-request memory allocation

Not suitable for:
- Long-lived objects with varied lifetimes
- Scenarios requiring individual deallocation
- Memory-constrained embedded systems

## References

- LLVM BumpPtrAllocator
- Apache APR Memory Pools
- Rust typed-arena
- jemalloc Arena Design