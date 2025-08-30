cmake_minimum_required(VERSION 3.20)

# Arena test executables
add_executable(test_arena_basic test_arena_basic.cpp)
target_link_libraries(test_arena_basic db25arena gtest_main)
target_compile_options(test_arena_basic PRIVATE 
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow 
    -Wunused -Wnon-virtual-dtor -Wold-style-cast 
    -Wcast-align -Woverloaded-virtual -Wsign-conversion
)

add_executable(test_arena_stress test_arena_stress.cpp)
target_link_libraries(test_arena_stress db25arena gtest_main)
target_compile_options(test_arena_stress PRIVATE 
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow 
    -Wunused -Wnon-virtual-dtor -Wold-style-cast 
    -Wcast-align -Woverloaded-virtual -Wsign-conversion
)

add_executable(test_arena_edge test_arena_edge.cpp)
target_link_libraries(test_arena_edge db25arena gtest_main)
target_compile_options(test_arena_edge PRIVATE 
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow 
    -Wunused -Wnon-virtual-dtor -Wold-style-cast 
    -Wcast-align -Woverloaded-virtual -Wsign-conversion
)

# Add tests to CTest
add_test(NAME ArenaBasicTest COMMAND test_arena_basic)
add_test(NAME ArenaStressTest COMMAND test_arena_stress)
add_test(NAME ArenaEdgeTest COMMAND test_arena_edge)

# Create a custom target to run all arena tests
add_custom_target(test_arena
    COMMAND test_arena_basic
    COMMAND test_arena_stress  
    COMMAND test_arena_edge
    DEPENDS test_arena_basic test_arena_stress test_arena_edge
    COMMENT "Running all arena tests..."
)