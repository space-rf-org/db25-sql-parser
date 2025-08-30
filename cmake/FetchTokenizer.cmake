# DB25 Tokenizer Dependency Management
# This module handles fetching and configuring the DB25 tokenizer dependency
# in a clean, maintainable way that follows CMake best practices.

include(FetchContent)

# Version management - pin to specific version for reproducibility
set(DB25_TOKENIZER_VERSION "main" CACHE STRING "Version of DB25 tokenizer to use")
set(DB25_TOKENIZER_GIT_URL "https://github.com/space-rf-org/DB25-sql-tokenizer.git" 
    CACHE STRING "Git repository URL for DB25 tokenizer")

# Configure FetchContent for the tokenizer
FetchContent_Declare(
    db25_tokenizer
    GIT_REPOSITORY ${DB25_TOKENIZER_GIT_URL}
    GIT_TAG        ${DB25_TOKENIZER_VERSION}
    GIT_SHALLOW    TRUE  # Only fetch the specified branch/tag
    GIT_PROGRESS   TRUE  # Show download progress
    
    # Don't update automatically - we want reproducible builds
    UPDATE_DISCONNECTED TRUE
    
    # Configure where to place the source
    SOURCE_DIR     ${CMAKE_BINARY_DIR}/_deps/db25_tokenizer-src
    BINARY_DIR     ${CMAKE_BINARY_DIR}/_deps/db25_tokenizer-build
    SUBBUILD_DIR   ${CMAKE_BINARY_DIR}/_deps/db25_tokenizer-subbuild
)

# Set options for the tokenizer before fetching
# This prevents the tokenizer from building its own tests/examples
set(BUILD_TESTING_SAVED ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE INTERNAL "")

# The tokenizer uses BUILD_TESTS not DB25_TOKENIZER_BUILD_TESTS
set(BUILD_TESTS OFF CACHE BOOL "Build test programs" FORCE)

# Modern way to fetch and make available
FetchContent_MakeAvailable(db25_tokenizer)

# Restore BUILD_TESTS for our own tests
set(BUILD_TESTS ON CACHE BOOL "Build test programs" FORCE)

# Restore original BUILD_TESTING value
set(BUILD_TESTING ${BUILD_TESTING_SAVED} CACHE BOOL "Build tests" FORCE)

# Export variables for use in main CMakeLists.txt
# These are set in the cache for global access
set(DB25_TOKENIZER_INCLUDE_DIR ${db25_tokenizer_SOURCE_DIR}/include CACHE INTERNAL "Tokenizer include directory")
set(DB25_TOKENIZER_SOURCE_DIR ${db25_tokenizer_SOURCE_DIR} CACHE INTERNAL "Tokenizer source directory")

# Create an interface library for clean dependency management
if(NOT TARGET db25::tokenizer_headers)
    add_library(db25::tokenizer_headers INTERFACE IMPORTED GLOBAL)
    set_target_properties(db25::tokenizer_headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${db25_tokenizer_SOURCE_DIR}/include"
    )
endif()

# Function to get tokenizer source files
# This allows the main CMakeLists.txt to cleanly get the sources it needs
function(get_tokenizer_sources OUTPUT_VAR)
    set(${OUTPUT_VAR}
        ${db25_tokenizer_SOURCE_DIR}/src/simd_tokenizer.cpp
        PARENT_SCOPE
    )
endfunction()

# Print status for debugging
message(STATUS "DB25 Tokenizer Configuration:")
message(STATUS "  Repository: ${DB25_TOKENIZER_GIT_URL}")
message(STATUS "  Version: ${DB25_TOKENIZER_VERSION}")
message(STATUS "  Source Dir: ${db25_tokenizer_SOURCE_DIR}")
message(STATUS "  Include Dir: ${db25_tokenizer_SOURCE_DIR}/include")