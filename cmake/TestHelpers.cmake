# Test Helper Macros for DB25 Parser
# Provides clean macros for creating test executables with proper dependencies

# Function to add a parser test executable with all necessary dependencies
function(add_parser_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        db25parser 
        gtest_main
        ${ARGN}  # Any additional libraries
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Function for tests that don't use gtest
function(add_parser_test_no_gtest TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        db25parser
        ${ARGN}  # Any additional libraries
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Function for arena-specific tests
function(add_arena_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        db25arena
        gtest_main
        ${ARGN}
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()