#include <gtest/gtest.h>
#include "db25/memory/arena.hpp"
#include <chrono>
#include <vector>

using namespace db25;

TEST(ArenaTest, BasicAllocation) {
    Arena arena;
    
    void* ptr1 = arena.allocate(16);
    ASSERT_NE(ptr1, nullptr);
    
    void* ptr2 = arena.allocate(32);
    ASSERT_NE(ptr2, nullptr);
    ASSERT_NE(ptr1, ptr2);
    
    EXPECT_GE(arena.total_allocated(), 16 + 32);
}

TEST(ArenaTest, AlignedAllocation) {
    Arena arena;
    
    void* ptr1 = arena.allocate(7, 8);
    ASSERT_NE(ptr1, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr1) % 8, 0);
    
    void* ptr2 = arena.allocate(13, 16);
    ASSERT_NE(ptr2, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % 16, 0);
    
    void* ptr3 = arena.allocate(1, 64);
    ASSERT_NE(ptr3, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr3) % 64, 0);
}

TEST(ArenaTest, LargeAllocation) {
    Arena arena;
    
    constexpr size_t large_size = 128 * 1024; // 128KB
    void* ptr = arena.allocate(large_size);
    ASSERT_NE(ptr, nullptr);
    
    EXPECT_GE(arena.total_allocated(), large_size);
}

TEST(ArenaTest, Reset) {
    Arena arena;
    
    arena.allocate(1024);
    arena.allocate(2048);
    
    const size_t allocated = arena.total_allocated();
    const size_t used = arena.total_used();
    EXPECT_GT(used, 0);
    
    arena.reset();
    
    EXPECT_EQ(arena.total_allocated(), allocated);
    EXPECT_EQ(arena.total_used(), 0);
    
    void* ptr = arena.allocate(16);
    ASSERT_NE(ptr, nullptr);
}

TEST(ArenaTest, Clear) {
    Arena arena;
    
    arena.allocate(1024);
    arena.allocate(2048);
    
    EXPECT_GT(arena.total_allocated(), 0);
    EXPECT_GT(arena.total_used(), 0);
    
    arena.clear();
    
    EXPECT_EQ(arena.total_allocated(), 0);
    EXPECT_EQ(arena.total_used(), 0);
}

TEST(ArenaTest, MoveConstructor) {
    Arena arena1;
    arena1.allocate(1024);
    
    const size_t allocated = arena1.total_allocated();
    const size_t used = arena1.total_used();
    
    Arena arena2(std::move(arena1));
    
    EXPECT_EQ(arena2.total_allocated(), allocated);
    EXPECT_EQ(arena2.total_used(), used);
    
    EXPECT_EQ(arena1.total_allocated(), 0);
    EXPECT_EQ(arena1.total_used(), 0);
}

TEST(ArenaTest, MoveAssignment) {
    Arena arena1;
    arena1.allocate(1024);
    
    Arena arena2;
    arena2.allocate(512);
    
    const size_t allocated = arena1.total_allocated();
    const size_t used = arena1.total_used();
    
    arena2 = std::move(arena1);
    
    EXPECT_EQ(arena2.total_allocated(), allocated);
    EXPECT_EQ(arena2.total_used(), used);
    
    EXPECT_EQ(arena1.total_allocated(), 0);
    EXPECT_EQ(arena1.total_used(), 0);
}

TEST(ArenaTest, AllocationPerformance) {
    Arena arena;
    
    constexpr size_t num_allocations = 100000;
    constexpr size_t allocation_size = 64;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_allocations; ++i) {
        [[maybe_unused]] void* ptr = arena.allocate(allocation_size);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double ns_per_alloc = static_cast<double>(duration.count()) / num_allocations;
    
    std::cout << "Allocation time: " << ns_per_alloc << " ns per allocation\n";
    std::cout << "Total allocated: " << arena.total_allocated() << " bytes\n";
    std::cout << "Total used: " << arena.total_used() << " bytes\n";
    
    EXPECT_LT(ns_per_alloc, 10.0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}