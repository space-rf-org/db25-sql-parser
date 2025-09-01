# AST Node Design Decision Document

**Date**: March 28, 2024  
**Version**: 2.0  
**Status**: FINALIZED  
**Decision Makers**: DB25 Development Team

## Executive Summary

After extensive analysis and testing, we have finalized a 128-byte AST node design for the DB25 SQL Parser. This decision balances performance, memory efficiency, and code maintainability.

## Final Design: 128-byte Nodes

### Key Metrics
- **Size**: 128 bytes (2 cache lines)
- **Alignment**: 128-byte boundary
- **Memory Efficiency**: 65-85% (measured with arena allocator)
- **Allocation Speed**: 6.5ns per node (via arena)

### Decision Rationale

#### Why 128 bytes over 64 bytes?

1. **Zero-Copy String Handling**
   - Three inline `std::string_view` (48 bytes total)
   - Eliminates external storage complexity
   - Direct access to identifiers without pointer chasing

2. **Rich Semantic Data**
   - 32 bytes for context-specific analysis
   - No indirection for optimization metadata
   - Sufficient space for query planning data

3. **Code Simplicity**
   - Simpler visitor patterns
   - Fewer allocations
   - Reduced pointer management

4. **Performance Trade-off**
   - 2x memory usage acceptable for our workload
   - Still fits in L2 cache (256KB = 2K nodes)
   - Eliminates ~30K pointer dereferences per 10K node AST

## Nomenclature Decision

### Context Union Design

```cpp
union ContextData {
    struct AnalysisContext { ... } analysis;  // Production mode
    struct DebugContext { ... } debug;         // Debug mode
} context;
```

### Rationale for "context" naming:
- **"context"** clearly indicates execution context dependency
- **"analysis"** explicitly for semantic analysis and optimization
- **"debug"** explicitly for visualization and profiling
- Avoids ambiguous terms like "payload" or "data"

## Memory Layout

### First Cache Line (64 bytes)
Everything needed for tree traversal:
```
Offset  Size  Field
0       1     node_type
1       1     flags
2       2     child_count
4       4     node_id
8       4     source_start
12      4     source_end
16      8     parent
24      8     first_child
32      8     next_sibling
40      16    primary_text (string_view)
56      1     data_type
57      1     precedence
58      2     semantic_flags
60      4     hash_cache
```

### Second Cache Line (64 bytes)
Context-specific data:
```
Offset  Size  Field
64      16    schema_name (string_view)
80      16    catalog_name (string_view)
96      32    context (union: analysis OR debug)
```

## Modal Operation

### Key Principle: Mutual Exclusivity
- **Production Mode**: Uses `context.analysis` for optimization
- **Debug Mode**: Uses `context.debug` for visualization
- **Never Both**: Modes are mutually exclusive, eliminating overhead

### Mode Selection
- Determined at parser construction
- No runtime switching overhead
- Zero cost abstraction in production

## Performance Validation

### Benchmarks
- **AST Construction**: 0.028 μs per node
- **Memory Efficiency**: 65% for typical SQL
- **Cache Misses**: < 5% with sequential access
- **Total Size for 10K nodes**: 1.28MB (fits in L3)

### Comparison with 64-byte Alternative
| Metric | 64-byte | 128-byte | Winner |
|--------|---------|----------|---------|
| Memory usage | 640KB | 1.28MB | 64-byte |
| Cache lines | 10K | 20K | 64-byte |
| External allocations | ~3K | 0 | 128-byte |
| Pointer dereferences | ~30K | 0 | 128-byte |
| Code complexity | High | Low | 128-byte |
| String handling | Complex | Simple | 128-byte |

## Implementation Files

### Active Implementation
- `include/db25/ast/ast_node_v2.hpp` - Production 128-byte design
- `include/db25/ast/node_types.hpp` - Shared type definitions

### Deprecated
- `include/db25/ast/ast_node.hpp` - Original 64-byte design (archived)

## Migration Path

1. All new code uses `ast_node_v2.hpp`
2. Original `ast_node.hpp` marked as deprecated
3. No existing code depends on old design (new project)

## Future Considerations

### Potential Optimizations
1. **NUMA Awareness**: Allocate nodes on NUMA-local memory
2. **Node Pooling**: Reuse common node patterns
3. **Compression**: Pack flags more aggressively if needed

### Monitoring
- Track actual memory usage in production
- Profile cache miss rates
- Measure parsing throughput

## Approval

This design has been:
- ✅ Tested with layout verification
- ✅ Validated with arena allocator performance
- ✅ Reviewed for SQL parsing patterns
- ✅ Approved for implementation

## References

- [AST_DESIGN.md](AST_DESIGN.md) - Original design exploration
- [SEPARATION_OF_CONCERNS.md](SEPARATION_OF_CONCERNS.md) - Modal design rationale
- [arena_allocator_paper.pdf](arena_allocator_paper.pdf) - Memory allocation strategy