# DB25 SQL Parser - Build & Test Instructions

## 🚀 Quick Start (Recommended)

```bash
# Clone and build everything with one command
git clone https://github.com/yourusername/DB25-parser2.git
cd DB25-parser2
./build.sh Release yes yes  # Build type, run tests, generate dumps
```

## 📦 Prerequisites

### Required
- **C++23 Compiler**: GCC 13+, Clang 15+, or MSVC 2022+
- **CMake**: Version 3.20 or higher
- **Make**: GNU Make or Ninja
- **SIMD Support**: SSE4.2 minimum (AVX2 recommended)

### Optional
- **Google Test**: Auto-downloaded if BUILD_TESTS=ON
- **Git**: For cloning the repository

### Platform-Specific

#### macOS
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install CMake via Homebrew
brew install cmake
```

#### Linux (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install build-essential cmake git
```

#### Linux (Fedora/RHEL)
```bash
sudo dnf install gcc-c++ cmake git make
```

#### Windows
- Install Visual Studio 2022 with C++ workload
- Or use MSYS2/MinGW-w64 with CMake

## 🔨 Build Instructions

### Method 1: Using Build Script (Easiest)

```bash
# Default Release build with tests
./build.sh

# Debug build without tests
./build.sh Debug no

# Release build with tests and AST dumps
./build.sh Release yes yes
```

### Method 2: Manual CMake Build

```bash
# 1. Clean any previous builds
rm -rf build

# 2. Create and enter build directory
mkdir build && cd build

# 3. Configure (choose one)
# Release build (recommended for performance)
cmake -DCMAKE_BUILD_TYPE=Release ..

# Debug build (for development)
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Custom options
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTS=ON \
      -DBUILD_TOOLS=ON \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..

# 4. Build
make -j$(nproc)  # Linux/macOS
make -j%NUMBER_OF_PROCESSORS%  # Windows

# 5. Install (optional)
sudo make install
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Release | Build type (Debug/Release/RelWithDebInfo) |
| `BUILD_TESTS` | ON | Build test suites |
| `BUILD_TOOLS` | ON | Build AST viewer and tools |
| `BUILD_BENCHMARKS` | OFF | Build performance benchmarks |
| `CMAKE_INSTALL_PREFIX` | /usr/local | Installation directory |

## 🧪 Testing

### Run All Tests

```bash
cd build
make test
# or
ctest --verbose
```

### Run Specific Test Suites

```bash
cd build

# Basic parser tests
./test_parser_basic

# Join parsing tests
./test_join_comprehensive

# Window function tests
./test_window_functions

# CTE tests
./test_cte

# Group by tests
./test_group_by

# Edge cases
./test_edge_cases
```

### Generate Visual AST Dumps

```bash
cd build

# Generate all dumps
make generate_ast_dumps

# View generated dumps
ls /tmp/db25_ast_dumps/
cat /tmp/db25_ast_dumps/simple_select_ast.txt
```

## 🎨 Using the AST Viewer

### Basic Usage

```bash
cd build

# Parse from command line
echo "SELECT * FROM users" | ./ast_viewer

# Parse from file (specific query)
./ast_viewer --file ../tests/showcase_queries.sqls --index 1

# Parse all queries in file
./ast_viewer --file ../tests/showcase_queries.sqls --all

# With statistics
./ast_viewer --stats --file ../tests/showcase_queries.sqls --index 1

# Without colors (for piping)
echo "SELECT * FROM users" | ./ast_viewer --no-color > output.txt
```

### Example Workflow

```bash
# 1. Write SQL to file
cat > test.sql << 'EOF'
SELECT u.name, COUNT(o.id) as order_count
FROM users u
LEFT JOIN orders o ON u.id = o.user_id
WHERE u.created_at >= '2024-01-01'
GROUP BY u.id, u.name
HAVING COUNT(o.id) > 5
ORDER BY order_count DESC
EOF

# 2. Visualize AST
./ast_viewer < test.sql

# 3. Save AST to file
./ast_viewer --no-color < test.sql > test_ast.txt
```

## ✅ Verification Steps

After building, verify everything works:

```bash
# 1. Check parser library
ls -la libdb25parser.a

# 2. Test simple parse
echo "SELECT 1" | ./ast_viewer

# 3. Run basic tests
./test_parser_basic

# 4. Check complex query
echo "WITH RECURSIVE t(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM t WHERE n<10) SELECT * FROM t" | ./ast_viewer

# 5. Verify all test suites pass
make test
```

## 🐛 Troubleshooting

### Common Issues

#### CMake Version Too Old
```bash
# Error: CMake 3.20 or higher is required

# Solution: Update CMake
# macOS: brew upgrade cmake
# Linux: Download from https://cmake.org/download/
```

#### C++23 Not Supported
```bash
# Error: C++23 standard requested but not supported

# Solution: Update compiler
# macOS: Update Xcode
# Linux: Install GCC 13+ or Clang 15+
```

#### SIMD Instructions Not Supported
```bash
# Warning: SSE4.2/AVX2 not detected

# This is OK - parser will use scalar fallback
# For best performance, use a modern CPU
```

#### Tests Failing
```bash
# Some tests fail

# 1. Check build type (Debug recommended for testing)
cmake -DCMAKE_BUILD_TYPE=Debug ..

# 2. Run individual test with output
./test_parser_basic --gtest_filter=*YourTest* --gtest_output=xml

# 3. Check for INSERT column list issue (known limitation)
```

### Debug Build

For development and debugging:

```bash
# Configure debug build
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-g -O0 -fsanitize=address" \
      ..

# Build with verbose output
make VERBOSE=1

# Run with debugger
gdb ./test_parser_basic
lldb ./test_parser_basic
```

## 📊 Performance Testing

```bash
# Build with optimizations
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native" \
      ..
make -j$(nproc)

# Run performance tests
./test_performance

# Profile with perf (Linux)
perf record ./test_performance
perf report

# Profile with Instruments (macOS)
instruments -t "Time Profiler" ./test_performance
```

## 🚢 Deployment

### Installing System-Wide

```bash
cd build
sudo make install

# Verify installation
ls -la /usr/local/lib/libdb25parser.a
ls -la /usr/local/include/db25/
ls -la /usr/local/bin/ast_viewer
```

### Using in Your Project

```cmake
# In your CMakeLists.txt
find_package(DB25Parser REQUIRED)
target_link_libraries(your_app PRIVATE db25parser)
```

Or manually:

```bash
g++ -std=c++23 -I/usr/local/include your_app.cpp -L/usr/local/lib -ldb25parser
```

## 📝 Summary Checklist

- [ ] Prerequisites installed (C++23 compiler, CMake 3.20+)
- [ ] Repository cloned
- [ ] Build successful (`./build.sh` or manual)
- [ ] Tests passing (`make test`)
- [ ] AST viewer working (`echo "SELECT 1" | ./ast_viewer`)
- [ ] AST dumps generated (`make generate_ast_dumps`)
- [ ] Complex queries parsing correctly
- [ ] Documentation reviewed

## 🎉 Success!

If all steps complete successfully, the DB25 SQL Parser is ready to use. Check out:
- [User Manual](docs/USER_MANUAL.md) for usage examples
- [Developer Guide](docs/DEVELOPER_GUIDE.md) for extending the parser
- [Test queries](tests/showcase_queries.sqls) for SQL examples

## Need Help?

- Check [Troubleshooting](#-troubleshooting) above
- Review [Documentation](docs/)
- Open an [Issue](https://github.com/yourusername/DB25-parser2/issues)