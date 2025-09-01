#!/bin/bash

# DB25 SQL Parser - Build Script
# This script performs a clean build and runs tests

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Configuration
BUILD_TYPE="${1:-Release}"
RUN_TESTS="${2:-yes}"
GENERATE_DUMPS="${3:-no}"

echo -e "${BOLD}${BLUE}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}${BLUE}║          DB25 SQL Parser - Build System                     ║${NC}"
echo -e "${BOLD}${BLUE}╚══════════════════════════════════════════════════════════════╝${NC}"
echo

# Display configuration
echo -e "${YELLOW}Build Configuration:${NC}"
echo -e "  Build Type:     ${BOLD}$BUILD_TYPE${NC}"
echo -e "  Run Tests:      ${BOLD}$RUN_TESTS${NC}"
echo -e "  Generate Dumps: ${BOLD}$GENERATE_DUMPS${NC}"
echo

# Clean previous build
if [ -d "build" ]; then
    echo -e "${YELLOW}Cleaning previous build...${NC}"
    rm -rf build
fi

# Create build directory
echo -e "${YELLOW}Creating build directory...${NC}"
mkdir -p build
cd build

# Configure with CMake
echo -e "${YELLOW}Configuring with CMake...${NC}"
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
      -DBUILD_TESTS=ON \
      -DBUILD_TOOLS=ON \
      .. || {
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
}

# Build
echo -e "${YELLOW}Building project...${NC}"
make -j$(nproc) || {
    echo -e "${RED}Build failed!${NC}"
    exit 1
}

echo -e "${GREEN}✓ Build successful!${NC}"

# Run tests if requested
if [ "$RUN_TESTS" = "yes" ]; then
    echo
    echo -e "${YELLOW}Running tests...${NC}"
    make test || {
        echo -e "${RED}Some tests failed!${NC}"
        # Don't exit, show summary
    }
    
    # Run individual test suites for more detail
    echo
    echo -e "${YELLOW}Test Suite Results:${NC}"
    
    for test in test_parser_basic test_join_comprehensive test_window_functions test_cte test_group_by; do
        if [ -f "./$test" ]; then
            echo -n -e "  $test: "
            if ./$test > /dev/null 2>&1; then
                echo -e "${GREEN}✓ PASSED${NC}"
            else
                echo -e "${RED}✗ FAILED${NC}"
            fi
        fi
    done
fi

# Generate AST dumps if requested
if [ "$GENERATE_DUMPS" = "yes" ]; then
    echo
    echo -e "${YELLOW}Generating AST dumps...${NC}"
    make generate_ast_dumps || {
        echo -e "${RED}Failed to generate dumps!${NC}"
    }
    echo -e "${GREEN}✓ AST dumps generated in /tmp/db25_ast_dumps/${NC}"
fi

# Test AST viewer
echo
echo -e "${YELLOW}Testing AST Viewer...${NC}"
if echo "SELECT * FROM test" | ./ast_viewer --no-color > /dev/null 2>&1; then
    echo -e "${GREEN}✓ AST Viewer working${NC}"
else
    echo -e "${RED}✗ AST Viewer not working${NC}"
fi

# Show summary
echo
echo -e "${BOLD}${BLUE}════════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}${BOLD}Build Complete!${NC}"
echo
echo -e "${YELLOW}Next steps:${NC}"
echo -e "  1. Test the parser:  ${BOLD}echo 'SELECT * FROM users' | ./ast_viewer${NC}"
echo -e "  2. View examples:    ${BOLD}./ast_viewer --file ../tests/showcase_queries.sqls --index 1${NC}"
echo -e "  3. Run all examples: ${BOLD}./ast_viewer --file ../tests/showcase_queries.sqls --all${NC}"
echo -e "  4. Install (optional): ${BOLD}sudo make install${NC}"
echo
echo -e "${YELLOW}Documentation:${NC}"
echo -e "  User Manual:    ${BOLD}docs/USER_MANUAL.md${NC}"
echo -e "  Developer Guide: ${BOLD}docs/DEVELOPER_GUIDE.md${NC}"
echo
echo -e "${BOLD}${BLUE}════════════════════════════════════════════════════════════════${NC}"

# Return to original directory
cd ..

# Make script executable for future use
chmod +x build.sh 2>/dev/null || true

exit 0