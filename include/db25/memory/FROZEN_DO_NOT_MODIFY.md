# ⚠️ FROZEN CODE - DO NOT MODIFY ⚠️

## Arena Allocator - Frozen Status

**Status**: FROZEN  
**Date**: March 28, 2024  
**Version**: 1.0.1  
**Test Coverage**: 100% (67/67 tests passing)

## Files Under Freeze

The following files are frozen and must NOT be modified without a formal design change process:

### Core Implementation
- `arena.hpp` - Arena allocator header
- `../../src/memory/arena.cpp` - Arena allocator implementation

### Test Suite  
- `../../tests/arena/test_arena_basic.cpp` - Basic functionality tests (26 tests)
- `../../tests/arena/test_arena_stress.cpp` - Performance tests (13 tests)
- `../../tests/arena/test_arena_edge.cpp` - Edge case tests (28 tests)
- `../../tests/arena/CMakeLists.txt` - Test build configuration

## Modification Process

Any changes to frozen files require:

1. **Design Proposal**: Written proposal documenting:
   - Rationale for change
   - Impact analysis
   - Performance implications
   - Backward compatibility

2. **Review Process**:
   - Technical review by maintainers
   - Performance regression testing
   - Full test suite must pass

3. **Documentation Update**:
   - Update version number
   - Update this freeze document
   - Update academic paper if needed

## Performance Baseline

Current performance metrics that must be maintained:

| Metric | Baseline |
|--------|----------|
| Allocation latency | < 10ns |
| Throughput | > 150M ops/s |
| Memory efficiency | > 85% |
| Test pass rate | > 98% |

## Contact

For modification requests, contact:
- Development Team: Space-RF.org
- Technical Lead: DB25 Parser Team

## Freeze Rationale

The arena allocator has been:
- Thoroughly tested (67 test cases)
- Performance validated (6.5ns allocation)
- Academically documented
- Production ready

Changes risk introducing subtle bugs or performance regressions in this critical component.

## Approved Modifications

### v1.0.1 — Correct over-alignment (alignment > CACHE_LINE_SIZE)

**Rationale**: `allocate(size, alignment)` returned a pointer that was only correct
when `alignment <= CACHE_LINE_SIZE` (64). It aligned the *offset within a block*
(`align_up(used, alignment)`) and returned `data + offset`, but block bases come
from `aligned_alloc(CACHE_LINE_SIZE, …)` and are only 64-byte aligned — so any
larger alignment (e.g. `ASTNode`, which is `alignas(128)`, or an explicit 256/page
request) yielded a misaligned pointer. Two edge-case tests (`OverAligned`,
`MaxAlignment`) exposed this.

**Change**: align the *absolute address* — `align_up(base + used, alignment) - base`
— so the returned pointer is correct for any alignment. Block bases are left at
their natural (non-page) addresses on purpose: varying bases give each block a
distinct cache colour, so nodes spread across all L1 sets (page-aligning every
block measurably slowed node-heavy parses). The dedicated/large-block path now
reserves `alignment - CACHE_LINE_SIZE` bytes of headroom so an over-aligned payload
cannot pad past the block end.

**Impact / compatibility**: no API or layout change; behaviour differs only for
`alignment > 64`, which was previously incorrect. All prior call sites (alignment
`<= 64`) are byte-for-byte unchanged.

**Performance**: end-to-end parse throughput unchanged (≈0.6 MB/s within noise on
the comprehensive corpus); allocation latency baseline maintained.

**Test update**: `test_arena_edge.cpp::OverAligned` asserted the second allocation
consumed `>= 256` bytes — an artifact of offset-based padding that only held when
the block base happened to be 256-aligned. Corrected to the true invariant: an
over-aligned allocation consumes `[size, size + alignment - 1]` bytes (padding is
strictly less than one alignment). All 67 tests pass.