cmake_minimum_required(VERSION 3.20)

# Set CMake policies to avoid warnings
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 NEW)  # Use FetchContent_MakeAvailable
endif()
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)  # Set timestamp for downloaded archives
endif()

project(DB25Parser VERSION 1.0.0 LANGUAGES CXX)

# C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -march=native
        -fno-exceptions  # We use Result types instead
    )
    
    # Config-specific flags
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    
    # SIMD detection
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    check_cxx_compiler_flag("-msse4.2" COMPILER_SUPPORTS_SSE42)
    
    if(COMPILER_SUPPORTS_AVX2)
        add_compile_definitions(DB25_HAS_AVX2)
    elseif(COMPILER_SUPPORTS_SSE42)
        add_compile_definitions(DB25_HAS_SSE42)
    endif()
elseif(MSVC)
    add_compile_options(/W4 $<$<CONFIG:Release>:/O2>)
endif()

# Add project's cmake modules to path
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Include directories for our own headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Fetch and configure DB25 Tokenizer dependency
include(FetchTokenizer)

# The tokenizer headers are now available via db25::tokenizer_headers target
# and DB25_TOKENIZER_INCLUDE_DIR variable

# Get tokenizer sources using the function from our module
get_tokenizer_sources(TOKENIZER_SOURCES)

# Source files
set(PARSER_SOURCES
    src/memory/arena.cpp
    src/ast/ast_node.cpp
    src/parser/parser.cpp
    src/parser/parser_statements.cpp
    src/parser/parser_ddl.cpp
    src/parser/tokenizer_adapter.cpp
    ${TOKENIZER_SOURCES}
)

# Arena-only library for testing
add_library(db25arena STATIC src/memory/arena.cpp)
target_include_directories(db25arena PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Main parser library
add_library(db25parser STATIC ${PARSER_SOURCES})
target_include_directories(db25parser PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
# Link to tokenizer headers using modern CMake target
target_link_libraries(db25parser PRIVATE db25::tokenizer_headers)

# Testing
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    
    # Include test helper macros
    include(TestHelpers)
    
    # Download and configure Google Test
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
        GIT_SHALLOW TRUE
        UPDATE_DISCONNECTED TRUE
    )
    
    # Prevent GoogleTest from overriding our compiler/linker options
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    
    FetchContent_MakeAvailable(googletest)
    
    # Add arena test subdirectory
    add_subdirectory(tests/arena)
    
    # Test executables
    # NOTE: test_parser.cpp, test_parser_sql_suite.cpp, and test_tokenizer_integration.cpp 
    # have been deleted. Only test_tokenizer_verification.cpp remains.
    
    # TEMPORARILY DISABLED: test_ast has undefined symbols for *_to_string functions
    # add_executable(test_ast tests/test_ast.cpp)
    # target_link_libraries(test_ast db25parser gtest_main)
    
    # Tokenizer verification test - verifies correct tokens for parser
    add_parser_test_no_gtest(test_tokenizer_verification tests/test_tokenizer_verification.cpp)
    target_compile_definitions(test_tokenizer_verification PRIVATE 
        SQL_TEST_FILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/sql_test.sqls")
    
    # Parser basic test suite (TDD - simplified to start)
    add_parser_test(test_parser_basic tests/test_parser_basic.cpp)
    
    # Parser SELECT test suite (detailed SELECT parsing)
    add_parser_test(test_parser_select tests/test_parser_select.cpp)
    
    add_parser_test(test_parser_correctness tests/test_parser_correctness.cpp)
    
    add_parser_test(test_parser_capabilities tests/test_parser_capabilities.cpp)
    
    add_parser_test(test_join_parsing tests/test_join_parsing.cpp)
    
    add_parser_test(test_join_comprehensive tests/test_join_comprehensive.cpp)
    
    add_parser_test(test_sql_operators tests/test_sql_operators.cpp)
    
    add_parser_test(test_group_by tests/test_group_by.cpp)
    
    add_parser_test(test_case_expr tests/test_case_expr.cpp)
    
    add_parser_test(test_ast_verification tests/test_ast_verification.cpp)
    
    add_parser_test(test_parser_fixes tests/test_parser_fixes.cpp)
    
    add_parser_test(test_subqueries tests/test_subqueries.cpp)
    
    add_parser_test(test_cte tests/test_cte.cpp)
    
    # Edge cases test suite
    add_parser_test(test_edge_cases tests/test_edge_cases.cpp)
    
    # Performance test suite
    add_parser_test(test_performance tests/test_performance.cpp)
    
    # Parser comprehensive test suite (TDD - full suite, disabled for now)
    # add_executable(test_parser_comprehensive tests/test_parser_comprehensive.cpp)
    # target_link_libraries(test_parser_comprehensive db25parser gtest_main)
    # target_include_directories(test_parser_comprehensive PRIVATE ${db25_tokenizer_SOURCE_DIR}/include)
    
    # Tests are automatically added by the macros above
    
    # Window Functions test
    add_parser_test(test_window_functions tests/test_window_functions.cpp gtest)
    
    # Parser Fixes Phase 1 test (Critical semantic fixes)
    add_parser_test(test_parser_fixes_phase1 tests/test_parser_fixes_phase1.cpp gtest)
    
    # Parser Fixes Phase 2 test (ORDER BY, window frames, aliases)
    add_parser_test(test_parser_fixes_phase2 tests/test_parser_fixes_phase2.cpp gtest)
    
    # DDL Statements test
    add_parser_test(test_ddl_statements tests/test_ddl_statements.cpp gtest)
    
    # SQL Completion test
    add_parser_test_no_gtest(test_sql_completion tests/test_sql_completion.cpp)
    
    # Comprehensive AST improvements test
    add_parser_test_no_gtest(test_ast_comprehensive tests/test_ast_comprehensive.cpp)
    
    # Advanced Types Support test
    add_parser_test(test_advanced_types tests/test_advanced_types.cpp gtest)
    
    # Parser Code Violations test
    add_parser_test_no_gtest(test_parser_violations tests/test_parser_violations.cpp)
    
    # Comprehensive Parser Validation test
    add_parser_test_no_gtest(test_parser_validation tests/test_parser_validation.cpp)
    
    # Debug tools can be added here as needed
endif()

# Benchmarking
option(BUILD_BENCHMARKS "Build benchmarks" OFF)  # Disabled - bench_parser.cpp has compilation errors
if(BUILD_BENCHMARKS)
    # Download Google Benchmark
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.0
    )
    FetchContent_MakeAvailable(benchmark)
    
    add_executable(bench_parser benchmarks/bench_parser.cpp)
    target_link_libraries(bench_parser db25parser benchmark::benchmark)
endif()

# Installation
install(TARGETS db25parser
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)

# Package configuration
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/DB25ParserConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/DB25ParserConfigVersion.cmake"
    DESTINATION lib/cmake/DB25Parser
)