/*
 * Copyright (c) 2024 Space-RF.org
 * Copyright (c) 2024 Chiradip Mandal
 * 
 * DB25 Arena Allocator - Basic Functionality Test Suite
 * 
 * FROZEN CODE - DO NOT MODIFY WITHOUT DESIGN CHANGE PROCESS
 * Part of the production-ready arena allocator test suite.
 * Modifications require formal design review.
 */

#include <gtest/gtest.h>
#include "db25/memory/arena.hpp"
#include <cstring>
#include <vector>
#include <random>
#include <thread>
#include <numeric>
#include <algorithm>  // for std::sort, std::unique

using namespace db25;

class ArenaBasicTest : public ::testing::Test {
protected:
    void SetUp() override {
        arena = std::make_unique<Arena>();
    }
    
    void TearDown() override {
        arena.reset();
    }
    
    std::unique_ptr<Arena> arena;
};

// ============= Basic Allocation Tests =============

TEST_F(ArenaBasicTest, DefaultConstruction) {
    Arena default_arena;
    EXPECT_EQ(default_arena.total_used(), 0);
    EXPECT_GE(default_arena.total_allocated(), Arena::DEFAULT_BLOCK_SIZE);
}

TEST_F(ArenaBasicTest, SingleAllocation) {
    void* ptr = arena->allocate(16);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(arena->total_used(), 16);
}

TEST_F(ArenaBasicTest, ZeroSizeAllocation) {
    // Even zero-size should return valid pointer (like malloc)
    void* ptr = arena->allocate(0);
    ASSERT_NE(ptr, nullptr);
}

TEST_F(ArenaBasicTest, MultipleSmallAllocations) {
    std::vector<void*> ptrs;
    constexpr size_t count = 1000;
    
    for (size_t i = 0; i < count; ++i) {
        void* ptr = arena->allocate(8);
        ASSERT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
    }
    
    // Verify all pointers are unique
    std::sort(ptrs.begin(), ptrs.end());
    auto last = std::unique(ptrs.begin(), ptrs.end());
    EXPECT_EQ(last, ptrs.end()) << "Duplicate pointers detected";
    
    EXPECT_EQ(arena->total_used(), count * 8);
}

TEST_F(ArenaBasicTest, LargeAllocation) {
    constexpr size_t large_size = 1024 * 1024; // 1MB
    void* ptr = arena->allocate(large_size);
    ASSERT_NE(ptr, nullptr);
    EXPECT_GE(arena->total_allocated(), large_size);
}

TEST_F(ArenaBasicTest, VeryLargeAllocation) {
    constexpr size_t huge_size = 10 * 1024 * 1024; // 10MB
    void* ptr = arena->allocate(huge_size);
    ASSERT_NE(ptr, nullptr);
    EXPECT_GE(arena->total_allocated(), huge_size);
}

// ============= Alignment Tests =============

TEST_F(ArenaBasicTest, DefaultAlignment) {
    void* ptr = arena->allocate(7); // Odd size
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 8, 0) 
        << "Default alignment should be 8 bytes";
}

TEST_F(ArenaBasicTest, CustomAlignment8) {
    void* ptr = arena->allocate(5, 8);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 8, 0);
}

TEST_F(ArenaBasicTest, CustomAlignment16) {
    void* ptr = arena->allocate(10, 16);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 16, 0);
}

TEST_F(ArenaBasicTest, CustomAlignment32) {
    void* ptr = arena->allocate(20, 32);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 32, 0);
}

TEST_F(ArenaBasicTest, CustomAlignment64) {
    void* ptr = arena->allocate(50, 64);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 64, 0);
}

TEST_F(ArenaBasicTest, MixedAlignments) {
    void* ptr1 = arena->allocate(7, 1);
    void* ptr2 = arena->allocate(15, 8);
    void* ptr3 = arena->allocate(31, 16);
    void* ptr4 = arena->allocate(63, 32);
    void* ptr5 = arena->allocate(127, 64);
    
    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);
    ASSERT_NE(ptr3, nullptr);
    ASSERT_NE(ptr4, nullptr);
    ASSERT_NE(ptr5, nullptr);
    
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % 8, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr3) % 16, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr4) % 32, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr5) % 64, 0);
}

// ============= Power of 2 Alignment Tests =============

TEST_F(ArenaBasicTest, NonPowerOfTwoAlignment) {
    // Should handle non-power-of-2 alignments gracefully
    // Implementation might round up or assert
    EXPECT_DEBUG_DEATH(arena->allocate(10, 7), ".*") 
        << "Non-power-of-2 alignment should assert in debug";
}

// ============= Write/Read Tests =============

TEST_F(ArenaBasicTest, WriteAndReadBack) {
    struct TestData {
        int a;
        double b;
        char c[16];
    };
    
    auto* data = arena->allocate_object<TestData>();
    ASSERT_NE(data, nullptr);
    
    data->a = 42;
    data->b = 3.14159;
    std::strcpy(data->c, "Hello Arena!");
    
    EXPECT_EQ(data->a, 42);
    EXPECT_DOUBLE_EQ(data->b, 3.14159);
    EXPECT_STREQ(data->c, "Hello Arena!");
}

TEST_F(ArenaBasicTest, MultipleTypesAllocation) {
    int* i = arena->allocate_object<int>(42);
    double* d = arena->allocate_object<double>(3.14);
    std::string* s = arena->allocate_object<std::string>("test");
    
    ASSERT_NE(i, nullptr);
    ASSERT_NE(d, nullptr);
    ASSERT_NE(s, nullptr);
    
    EXPECT_EQ(*i, 42);
    EXPECT_DOUBLE_EQ(*d, 3.14);
    EXPECT_EQ(*s, "test");
}

// ============= Reset Tests =============

TEST_F(ArenaBasicTest, ResetMaintainsMemory) {
    arena->allocate(1024);
    const size_t allocated_before = arena->total_allocated();
    const size_t used_before = arena->total_used();
    
    EXPECT_GT(used_before, 0);
    
    arena->reset();
    
    EXPECT_EQ(arena->total_allocated(), allocated_before) 
        << "Reset should maintain allocated memory";
    EXPECT_EQ(arena->total_used(), 0) 
        << "Reset should clear used counter";
}

TEST_F(ArenaBasicTest, ReuseAfterReset) {
    // First round of allocations
    std::vector<int*> first_round;
    for (int i = 0; i < 100; ++i) {
        first_round.push_back(arena->allocate_object<int>(i));
    }
    
    arena->reset();
    
    // Second round should reuse same memory
    std::vector<int*> second_round;
    for (int i = 0; i < 100; ++i) {
        second_round.push_back(arena->allocate_object<int>(i * 2));
    }
    
    // Verify second round data
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(*second_round[static_cast<size_t>(i)], i * 2);
    }
}

// ============= Clear Tests =============

TEST_F(ArenaBasicTest, ClearReleasesMemory) {
    arena->allocate(1024);
    EXPECT_GT(arena->total_allocated(), 0);
    EXPECT_GT(arena->total_used(), 0);
    
    arena->clear();
    
    EXPECT_EQ(arena->total_allocated(), 0) 
        << "Clear should release all memory";
    EXPECT_EQ(arena->total_used(), 0) 
        << "Clear should reset used counter";
}

TEST_F(ArenaBasicTest, AllocationAfterClear) {
    arena->allocate(1024);
    arena->clear();
    
    // Should be able to allocate after clear
    void* ptr = arena->allocate(512);
    ASSERT_NE(ptr, nullptr);
    EXPECT_GE(arena->total_allocated(), 512);
}

// ============= Block Growth Tests =============

TEST_F(ArenaBasicTest, BlockGrowth) {
    std::vector<size_t> block_sizes;
    size_t last_allocated = 0;
    
    // Allocate enough to trigger multiple blocks
    for (int i = 0; i < 10; ++i) {
        arena->allocate(Arena::DEFAULT_BLOCK_SIZE / 2);
        
        if (arena->total_allocated() > last_allocated) {
            block_sizes.push_back(arena->total_allocated() - last_allocated);
            last_allocated = arena->total_allocated();
        }
    }
    
    // Verify geometric growth up to MAX_BLOCK_SIZE
    for (size_t i = 1; i < block_sizes.size(); ++i) {
        if (block_sizes[i] < Arena::MAX_BLOCK_SIZE) {
            EXPECT_GE(block_sizes[i], block_sizes[i-1]) 
                << "Blocks should grow geometrically";
        } else {
            EXPECT_EQ(block_sizes[i], Arena::MAX_BLOCK_SIZE) 
                << "Blocks should cap at MAX_BLOCK_SIZE";
        }
    }
}

// ============= Statistics Tests =============

TEST_F(ArenaBasicTest, UtilizationCalculation) {
    EXPECT_DOUBLE_EQ(arena->utilization(), 0.0) 
        << "Empty arena should have 0% utilization";
    
    arena->allocate(arena->total_allocated() / 2);
    EXPECT_NEAR(arena->utilization(), 0.5, 0.1) 
        << "Half-full arena should have ~50% utilization";
}

TEST_F(ArenaBasicTest, BlockCount) {
    EXPECT_EQ(arena->block_count(), 1) 
        << "New arena should have 1 block";
    
    // Force allocation of new block
    arena->allocate(Arena::DEFAULT_BLOCK_SIZE * 2);
    EXPECT_GT(arena->block_count(), 1) 
        << "Large allocation should create new block";
}

// ============= Move Semantics Tests =============

TEST_F(ArenaBasicTest, MoveConstructor) {
    arena->allocate(1024);
    const size_t allocated = arena->total_allocated();
    const size_t used = arena->total_used();
    const size_t blocks = arena->block_count();
    
    Arena moved_arena(std::move(*arena));
    
    EXPECT_EQ(moved_arena.total_allocated(), allocated);
    EXPECT_EQ(moved_arena.total_used(), used);
    EXPECT_EQ(moved_arena.block_count(), blocks);
    
    // Original should be empty
    EXPECT_EQ(arena->total_allocated(), 0);
    EXPECT_EQ(arena->total_used(), 0);
}

TEST_F(ArenaBasicTest, MoveAssignment) {
    arena->allocate(1024);
    const size_t allocated = arena->total_allocated();
    const size_t used = arena->total_used();
    
    Arena other_arena;
    other_arena.allocate(512);
    
    other_arena = std::move(*arena);
    
    EXPECT_EQ(other_arena.total_allocated(), allocated);
    EXPECT_EQ(other_arena.total_used(), used);
    
    EXPECT_EQ(arena->total_allocated(), 0);
    EXPECT_EQ(arena->total_used(), 0);
}

// ============= Thread Local Arena Tests =============

TEST_F(ArenaBasicTest, ThreadLocalArena) {
    auto& tl_arena1 = ThreadLocalArena::get();
    auto& tl_arena2 = ThreadLocalArena::get();
    
    EXPECT_EQ(&tl_arena1, &tl_arena2) 
        << "Should return same arena in same thread";
    
    void* ptr = tl_arena1.allocate(100);
    ASSERT_NE(ptr, nullptr);
}

TEST_F(ArenaBasicTest, ThreadLocalArenaDifferentThreads) {
    Arena* arena1 = nullptr;
    Arena* arena2 = nullptr;
    
    std::thread t1([&arena1]() {
        arena1 = &ThreadLocalArena::get();
    });
    
    std::thread t2([&arena2]() {
        arena2 = &ThreadLocalArena::get();
    });
    
    t1.join();
    t2.join();
    
    EXPECT_NE(arena1, arena2) 
        << "Different threads should have different arenas";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}