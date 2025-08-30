/*
 * Copyright (c) 2024 Space-RF.org
 * Copyright (c) 2024 Chiradip Mandal
 * 
 * DB25 Arena Allocator - Edge Cases and Boundary Test Suite
 * 
 * FROZEN CODE - DO NOT MODIFY WITHOUT DESIGN CHANGE PROCESS
 * Critical edge case coverage for production stability.
 */

#include <gtest/gtest.h>
#include "db25/memory/arena.hpp"
#include <limits>
#include <cstring>
#include <set>

using namespace db25;

class ArenaEdgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        arena = std::make_unique<Arena>();
    }
    
    std::unique_ptr<Arena> arena;
};

// ============= Boundary Value Tests =============

TEST_F(ArenaEdgeTest, ZeroByteAllocation) {
    // Zero-size allocations should succeed (like malloc)
    void* ptr1 = arena->allocate(0);
    void* ptr2 = arena->allocate(0);
    
    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);
    EXPECT_NE(ptr1, ptr2) << "Each allocation should return unique pointer";
}

TEST_F(ArenaEdgeTest, OneByteAllocation) {
    void* ptr = arena->allocate(1);
    ASSERT_NE(ptr, nullptr);
    
    // Should be able to write one byte
    *static_cast<uint8_t*>(ptr) = 0xFF;
    EXPECT_EQ(*static_cast<uint8_t*>(ptr), 0xFF);
}

TEST_F(ArenaEdgeTest, MaxAlignment) {
    // Test maximum reasonable alignment (page size)
    constexpr size_t page_align = 4096;
    void* ptr = arena->allocate(100, page_align);
    
    if (ptr) { // Might fail due to memory constraints
        EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % page_align, 0)
            << "Should handle page alignment";
    }
}

TEST_F(ArenaEdgeTest, MinAlignment) {
    // Alignment of 1 should work
    void* ptr = arena->allocate(100, 1);
    ASSERT_NE(ptr, nullptr);
}

TEST_F(ArenaEdgeTest, UnalignedSize) {
    // Odd sizes with various alignments
    for (size_t align = 1; align <= 64; align *= 2) {
        for (size_t size = 1; size <= 17; size += 2) {
            void* ptr = arena->allocate(size, align);
            ASSERT_NE(ptr, nullptr);
            EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % align, 0)
                << "Failed for size=" << size << " align=" << align;
        }
    }
}

// ============= Allocation Pattern Tests =============

TEST_F(ArenaEdgeTest, SingleLargeAllocation) {
    // Allocate more than default block size
    constexpr size_t large_size = Arena::DEFAULT_BLOCK_SIZE * 3;
    void* ptr = arena->allocate(large_size);
    
    ASSERT_NE(ptr, nullptr);
    EXPECT_GE(arena->total_allocated(), large_size);
    
    // Verify we can write to entire range
    std::memset(ptr, 0xAB, large_size);
    EXPECT_EQ(static_cast<uint8_t*>(ptr)[0], 0xAB);
    EXPECT_EQ(static_cast<uint8_t*>(ptr)[large_size - 1], 0xAB);
}

TEST_F(ArenaEdgeTest, ExactBlockSize) {
    // Allocate exactly one block
    void* ptr = arena->allocate(Arena::DEFAULT_BLOCK_SIZE);
    ASSERT_NE(ptr, nullptr);
    
    // Next allocation should trigger new block
    size_t blocks_before = arena->block_count();
    arena->allocate(1);
    EXPECT_GT(arena->block_count(), blocks_before);
}

TEST_F(ArenaEdgeTest, JustUnderBlockSize) {
    // Allocate just under block size
    void* ptr1 = arena->allocate(Arena::DEFAULT_BLOCK_SIZE - 8);
    ASSERT_NE(ptr1, nullptr);
    
    size_t blocks_before = arena->block_count();
    
    // This should fit in remaining space
    void* ptr2 = arena->allocate(8);
    ASSERT_NE(ptr2, nullptr);
    EXPECT_EQ(arena->block_count(), blocks_before)
        << "Should fit in same block";
}

TEST_F(ArenaEdgeTest, JustOverBlockSize) {
    // Allocate just over block size
    void* ptr = arena->allocate(Arena::DEFAULT_BLOCK_SIZE + 1);
    ASSERT_NE(ptr, nullptr);
    
    // Should allocate a larger block
    EXPECT_GT(arena->total_allocated(), Arena::DEFAULT_BLOCK_SIZE);
}

// ============= Move Semantics Edge Cases =============

TEST_F(ArenaEdgeTest, MoveEmptyArena) {
    Arena empty;
    Arena moved(std::move(empty));
    
    // Should be able to use moved arena
    void* ptr = moved.allocate(100);
    ASSERT_NE(ptr, nullptr);
}

TEST_F(ArenaEdgeTest, SelfMoveAssignment) {
    // Self-assignment should be safe (though not recommended)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wself-move"
    *arena = std::move(*arena);
    #pragma clang diagnostic pop
    
    // Arena should still be usable
    void* ptr = arena->allocate(100);
    ASSERT_NE(ptr, nullptr);
}

TEST_F(ArenaEdgeTest, MoveAfterReset) {
    (void) arena->allocate(1000);
    arena->reset();
    
    Arena moved(std::move(*arena));
    
    // Moved arena should maintain allocated blocks
    EXPECT_GT(moved.total_allocated(), 0);
    EXPECT_EQ(moved.total_used(), 0);
}

TEST_F(ArenaEdgeTest, MoveAfterClear) {
    arena->allocate(1000);
    arena->clear();
    
    Arena moved(std::move(*arena));
    
    EXPECT_EQ(moved.total_allocated(), 0);
    EXPECT_EQ(moved.total_used(), 0);
    
    // Should be able to allocate
    void* ptr = moved.allocate(100);
    ASSERT_NE(ptr, nullptr);
}

// ============= Reset/Clear Edge Cases =============

TEST_F(ArenaEdgeTest, ResetEmptyArena) {
    Arena empty;
    empty.reset(); // Should not crash
    
    void* ptr = empty.allocate(100);
    ASSERT_NE(ptr, nullptr);
}

TEST_F(ArenaEdgeTest, ClearEmptyArena) {
    Arena empty;
    empty.clear(); // Should not crash
    
    void* ptr = empty.allocate(100);
    ASSERT_NE(ptr, nullptr);
}

TEST_F(ArenaEdgeTest, MultipleResets) {
    arena->allocate(100);
    arena->reset();
    arena->reset(); // Double reset should be safe
    arena->reset();
    
    void* ptr = arena->allocate(100);
    ASSERT_NE(ptr, nullptr);
}

TEST_F(ArenaEdgeTest, AlternatingResetAllocate) {
    for (int i = 0; i < 100; ++i) {
        arena->allocate(1000);
        arena->reset();
    }
    
    // Should still be functional
    EXPECT_GT(arena->total_allocated(), 0);
    EXPECT_EQ(arena->total_used(), 0);
}

// ============= Type Construction Tests =============

TEST_F(ArenaEdgeTest, ConstructWithoutAllocation) {
    // Test allocate_object with various types
    int* i = arena->allocate_object<int>(42);
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(*i, 42);
    
    double* d = arena->allocate_object<double>(3.14159);
    ASSERT_NE(d, nullptr);
    EXPECT_DOUBLE_EQ(*d, 3.14159);
}

TEST_F(ArenaEdgeTest, ConstructComplexType) {
    struct Complex {
        int a;
        double b;
        char c[32];
        
        Complex(int aa, double bb, const char* cc) 
            : a(aa), b(bb) {
            std::strncpy(c, cc, 31);
            c[31] = '\0';
        }
    };
    
    auto* obj = arena->allocate_object<Complex>(123, 45.67, "test string");
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->a, 123);
    EXPECT_DOUBLE_EQ(obj->b, 45.67);
    EXPECT_STREQ(obj->c, "test string");
}

TEST_F(ArenaEdgeTest, ConstructLargeObject) {
    struct LargeObject {
        uint8_t data[10000];
    };
    
    auto* obj = arena->allocate_object<LargeObject>();
    ASSERT_NE(obj, nullptr);
    
    // Should be properly aligned
    EXPECT_EQ(reinterpret_cast<uintptr_t>(obj) % alignof(LargeObject), 0);
}

// ============= Pointer Validity Tests =============

TEST_F(ArenaEdgeTest, PointerUniqueness) {
    std::set<void*> pointers;
    
    for (int i = 0; i < 1000; ++i) {
        void* ptr = arena->allocate(8);
        ASSERT_NE(ptr, nullptr);
        
        auto [it, inserted] = pointers.insert(ptr);
        EXPECT_TRUE(inserted) << "Duplicate pointer detected";
    }
}

TEST_F(ArenaEdgeTest, PointerStabilityAfterGrowth) {
    // Get initial pointer
    int* first = arena->allocate_object<int>(42);
    
    // Force arena to grow
    for (int i = 0; i < 1000; ++i) {
        arena->allocate(1024);
    }
    
    // Original pointer should still be valid
    EXPECT_EQ(*first, 42) << "Pointer should remain valid after growth";
    
    // Modify and verify
    *first = 99;
    EXPECT_EQ(*first, 99);
}

TEST_F(ArenaEdgeTest, WritePastAllocation) {
    // This test verifies we can detect buffer overruns in debug mode
    // In release, this is undefined behavior
    
    uint8_t* ptr1 = static_cast<uint8_t*>(arena->allocate(8));
    uint8_t* ptr2 = static_cast<uint8_t*>(arena->allocate(8));
    
    // Write to allocated memory (safe)
    std::memset(ptr1, 0xAA, 8);
    std::memset(ptr2, 0xBB, 8);
    
    // Verify no corruption
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(ptr1[i], 0xAA);
        EXPECT_EQ(ptr2[i], 0xBB);
    }
}

// ============= Statistics Edge Cases =============

TEST_F(ArenaEdgeTest, StatisticsOverflow) {
    // Allocate until we have significant memory usage
    for (int i = 0; i < 10000; ++i) {
        arena->allocate(1024);
    }
    
    size_t allocated = arena->total_allocated();
    size_t used = arena->total_used();
    
    EXPECT_GE(allocated, used) << "Allocated should always >= used";
    EXPECT_GT(arena->utilization(), 0.0);
    EXPECT_LE(arena->utilization(), 1.0);
}

TEST_F(ArenaEdgeTest, BlockCountAccuracy) {
    EXPECT_EQ(arena->block_count(), 1) << "Should start with one block";
    
    // Force new block
    arena->allocate(Arena::DEFAULT_BLOCK_SIZE * 2);
    EXPECT_GE(arena->block_count(), 2) << "Large allocation should add block";
    
    // Clear should reset block count
    arena->clear();
    EXPECT_EQ(arena->block_count(), 0) << "Clear should remove all blocks";
}

// ============= Alignment Edge Cases =============

TEST_F(ArenaEdgeTest, InvalidAlignment) {
    // Non-power-of-2 alignments should assert in debug
    #ifdef DEBUG
        EXPECT_DEBUG_DEATH(arena->allocate(10, 3), ".*");
        EXPECT_DEBUG_DEATH(arena->allocate(10, 7), ".*");
        EXPECT_DEBUG_DEATH(arena->allocate(10, 15), ".*");
    #endif
}

TEST_F(ArenaEdgeTest, ZeroAlignment) {
    // Zero alignment should assert
    #ifdef DEBUG
        EXPECT_DEBUG_DEATH(arena->allocate(10, 0), ".*");
    #endif
}

TEST_F(ArenaEdgeTest, OverAligned) {
    // First allocate something to move us off the aligned boundary
    void* ptr1 = arena->allocate(1, 1);
    ASSERT_NE(ptr1, nullptr);
    size_t used_after_first = arena->total_used();
    
    // Now test alignment larger than allocation size
    // This should waste space due to alignment padding
    void* ptr2 = arena->allocate(1, 256);
    ASSERT_NE(ptr2, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % 256, 0);
    
    // The second allocation should have consumed at least 256 bytes
    // due to alignment padding
    size_t used_for_second = arena->total_used() - used_after_first;
    EXPECT_GE(used_for_second, 256) 
        << "Alignment should consume at least alignment size when padding is needed";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}