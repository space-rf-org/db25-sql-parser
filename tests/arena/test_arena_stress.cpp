/*
 * Copyright (c) 2024 Space-RF.org
 * Copyright (c) 2024 Chiradip Mandal
 * 
 * DB25 Arena Allocator - Stress and Performance Test Suite
 * 
 * FROZEN CODE - DO NOT MODIFY WITHOUT DESIGN CHANGE PROCESS
 * Performance baselines must be maintained.
 */

#include <gtest/gtest.h>
#include "db25/memory/arena.hpp"
#include <chrono>
#include <random>
#include <thread>
#include <atomic>
#include <algorithm>
#include <numeric>

using namespace db25;
using namespace std::chrono;

class ArenaStressTest : public ::testing::Test {
protected:
    static constexpr size_t STRESS_ITERATIONS = 100000;
    static constexpr size_t LARGE_ITERATIONS = 10000;
};

// ============= Performance Tests =============

TEST_F(ArenaStressTest, AllocationSpeed) {
    Arena arena;
    
    auto start = high_resolution_clock::now();
    
    for (size_t i = 0; i < STRESS_ITERATIONS; ++i) {
        [[maybe_unused]] void* ptr = arena.allocate(64);
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<nanoseconds>(end - start);
    
    double ns_per_alloc = static_cast<double>(duration.count()) / STRESS_ITERATIONS;
    
    std::cout << "Allocation performance:\n"
              << "  Time per allocation: " << ns_per_alloc << " ns\n"
              << "  Allocations per second: " 
              << (1e9 / ns_per_alloc) << "\n"
              << "  Total allocated: " << arena.total_allocated() << " bytes\n"
              << "  Utilization: " << (arena.utilization() * 100) << "%\n";
    
    // Target: < 5ns per allocation as per design
    EXPECT_LT(ns_per_alloc, 10.0) 
        << "Allocation should be < 10ns (target is < 5ns)";
}

TEST_F(ArenaStressTest, MixedSizeAllocationSpeed) {
    Arena arena;
    std::mt19937 gen(42);
    std::uniform_int_distribution<size_t> size_dist(1, 256);
    
    auto start = high_resolution_clock::now();
    
    for (size_t i = 0; i < STRESS_ITERATIONS; ++i) {
        [[maybe_unused]] void* ptr = arena.allocate(size_dist(gen));
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<nanoseconds>(end - start);
    
    double ns_per_alloc = static_cast<double>(duration.count()) / STRESS_ITERATIONS;
    
    std::cout << "Mixed size allocation: " << ns_per_alloc << " ns per allocation\n";
    
    EXPECT_LT(ns_per_alloc, 15.0) 
        << "Mixed size allocation should still be fast";
}

// ============= Memory Pattern Tests =============

TEST_F(ArenaStressTest, FragmentationTest) {
    Arena arena;
    std::mt19937 gen(42);
    std::uniform_int_distribution<size_t> size_dist(1, 1024);
    
    size_t total_requested = 0;
    
    for (size_t i = 0; i < LARGE_ITERATIONS; ++i) {
        size_t size = size_dist(gen);
        total_requested += size;
        [[maybe_unused]] void* ptr = arena.allocate(size);
    }
    
    double overhead = static_cast<double>(arena.total_allocated() - total_requested) 
                     / total_requested;
    
    std::cout << "Fragmentation test:\n"
              << "  Requested: " << total_requested << " bytes\n"
              << "  Allocated: " << arena.total_allocated() << " bytes\n"
              << "  Overhead: " << (overhead * 100) << "%\n";
    
    // Arena should have minimal overhead (mainly alignment and block overhead)
    EXPECT_LT(overhead, 0.5) << "Overhead should be < 50%";
}

TEST_F(ArenaStressTest, AlignmentOverhead) {
    Arena arena;
    size_t total_requested = 0;
    
    // Allocate many small odd-sized objects with high alignment
    for (size_t i = 0; i < LARGE_ITERATIONS; ++i) {
        size_t size = (i % 7) + 1; // 1-7 bytes
        size_t alignment = 64; // Cache line alignment
        total_requested += size;
        [[maybe_unused]] void* ptr = arena.allocate(size, alignment);
    }
    
    double overhead = static_cast<double>(arena.total_used() - total_requested) 
                     / total_requested;
    
    std::cout << "Alignment overhead for small objects with 64-byte alignment:\n"
              << "  Overhead: " << (overhead * 100) << "%\n";
    
    // This will have high overhead due to alignment padding
    EXPECT_LT(overhead, 64.0) << "Even with bad alignment, overhead should be bounded";
}

// ============= Extreme Tests =============

TEST_F(ArenaStressTest, MaximumAllocation) {
    Arena arena;
    
    // Try to allocate maximum reasonable size
    constexpr size_t max_size = 100 * 1024 * 1024; // 100MB
    void* ptr = arena.allocate(max_size);
    
    ASSERT_NE(ptr, nullptr) << "Should handle large allocations";
    EXPECT_GE(arena.total_allocated(), max_size);
    
    // Write to verify memory is actually allocated
    std::memset(ptr, 0xFF, max_size);
}

TEST_F(ArenaStressTest, ManySmallAllocations) {
    Arena arena;
    constexpr size_t count = 1000000; // 1 million allocations
    
    auto start = high_resolution_clock::now();
    
    for (size_t i = 0; i < count; ++i) {
        [[maybe_unused]] void* ptr = arena.allocate(8);
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start);
    
    std::cout << "1 million small allocations took: " 
              << duration.count() << " ms\n";
    
    EXPECT_LT(duration.count(), 100) 
        << "1 million allocations should take < 100ms";
}

TEST_F(ArenaStressTest, ResetPerformance) {
    Arena arena;
    
    // Fill arena
    for (size_t i = 0; i < LARGE_ITERATIONS; ++i) {
        arena.allocate(1024);
    }
    
    auto start = high_resolution_clock::now();
    arena.reset();
    auto end = high_resolution_clock::now();
    
    auto duration = duration_cast<microseconds>(end - start);
    
    std::cout << "Reset time: " << duration.count() << " μs\n";
    
    EXPECT_LT(duration.count(), 100) 
        << "Reset should be very fast (< 100μs)";
}

// ============= Pattern Recognition Tests =============

TEST_F(ArenaStressTest, ASTSimulationPattern) {
    // Simulate AST node allocation pattern
    Arena arena;
    struct SimulatedNode {
        uint8_t data[64]; // Cache-line sized like our AST nodes
    };
    
    std::vector<SimulatedNode*> nodes;
    nodes.reserve(10000);
    
    auto start = high_resolution_clock::now();
    
    // Simulate parsing a large query with 10K nodes
    for (size_t i = 0; i < 10000; ++i) {
        auto* node = arena.construct<SimulatedNode>();
        nodes.push_back(node);
        
        // Simulate some work on the node
        std::memset(node->data, static_cast<int>(i & 0xFF), sizeof(node->data));
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    
    std::cout << "AST simulation (10K nodes):\n"
              << "  Time: " << duration.count() << " μs\n"
              << "  Per node: " << (duration.count() / 10000.0) << " μs\n"
              << "  Memory used: " << arena.total_used() << " bytes\n"
              << "  Memory efficiency: " 
              << ((10000.0 * 64) / arena.total_allocated() * 100) << "%\n";
    
    // Should be able to allocate 10K nodes very quickly
    EXPECT_LT(duration.count(), 10000) 
        << "10K node allocations should take < 10ms";
}

// ============= Concurrent Access Tests =============

TEST_F(ArenaStressTest, ThreadLocalArenaStress) {
    constexpr int num_threads = 8;
    constexpr int allocations_per_thread = 10000;
    
    std::vector<std::thread> threads;
    std::atomic<int> total_allocations{0};
    
    auto worker = [&total_allocations, allocations_per_thread]() {
        auto& arena = ThreadLocalArena::get();
        
        for (int i = 0; i < allocations_per_thread; ++i) {
            void* ptr = arena.allocate(64);
            ASSERT_NE(ptr, nullptr);
            total_allocations.fetch_add(1);
        }
    };
    
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start);
    
    EXPECT_EQ(total_allocations.load(), num_threads * allocations_per_thread);
    
    std::cout << "Thread-local arena (" << num_threads << " threads):\n"
              << "  Total time: " << duration.count() << " ms\n"
              << "  Throughput: " 
              << (total_allocations.load() / (duration.count() / 1000.0)) 
              << " allocations/sec\n";
}

// ============= Edge Case Tests =============

TEST_F(ArenaStressTest, AlternatingLargeSmall) {
    Arena arena;
    
    for (size_t i = 0; i < 1000; ++i) {
        // Alternate between tiny and large allocations
        if (i % 2 == 0) {
            [[maybe_unused]] void* ptr = arena.allocate(1);
        } else {
            [[maybe_unused]] void* ptr = arena.allocate(8192);
        }
    }
    
    // Should handle alternating pattern efficiently
    EXPECT_GT(arena.block_count(), 1) 
        << "Should create multiple blocks for this pattern";
}

TEST_F(ArenaStressTest, PowerOfTwoSizes) {
    Arena arena;
    
    for (size_t size = 1; size <= 65536; size *= 2) {
        for (int i = 0; i < 10; ++i) {
            void* ptr = arena.allocate(size);
            ASSERT_NE(ptr, nullptr);
            
            // Verify alignment is at least as requested
            if (size >= 8) {
                EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 8, 0);
            }
        }
    }
}

TEST_F(ArenaStressTest, RandomAccessPattern) {
    Arena arena;
    std::mt19937 gen(42);
    std::uniform_int_distribution<size_t> size_dist(1, 4096);
    std::uniform_int_distribution<size_t> align_dist_exp(0, 6); // 2^0 to 2^6
    
    std::vector<void*> ptrs;
    
    for (size_t i = 0; i < LARGE_ITERATIONS; ++i) {
        size_t size = size_dist(gen);
        size_t alignment = 1 << align_dist_exp(gen); // 1, 2, 4, 8, 16, 32, 64
        
        void* ptr = arena.allocate(size, alignment);
        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % alignment, 0)
            << "Failed alignment check for size=" << size 
            << " align=" << alignment;
        
        ptrs.push_back(ptr);
        
        // Randomly write to some previous allocations to simulate access
        if (!ptrs.empty() && i % 100 == 0) {
            std::uniform_int_distribution<size_t> ptr_dist(0, ptrs.size() - 1);
            void* random_ptr = ptrs[ptr_dist(gen)];
            *static_cast<uint8_t*>(random_ptr) = static_cast<uint8_t>(i & 0xFF);
        }
    }
}

// ============= Utilization Tests =============

TEST_F(ArenaStressTest, HighUtilization) {
    Arena arena;
    
    // Allocate exactly one block worth of memory
    size_t allocated = 0;
    while (allocated < Arena::DEFAULT_BLOCK_SIZE - 64) {
        arena.allocate(64);
        allocated += 64;
    }
    
    double util = arena.utilization();
    std::cout << "Utilization with perfect 64-byte allocations: " 
              << (util * 100) << "%\n";
    
    EXPECT_GT(util, 0.9) << "Should achieve > 90% utilization with uniform sizes";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}