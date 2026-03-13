#!/bin/bash
# Build script for PM-NIXL

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== PM-NIXL Build Script ==="
echo ""

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo "Configuring with CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DBUILD_BENCHMARKS=OFF

# Build
echo ""
echo "Building..."
make -j$(nproc)

# Test
echo ""
echo "Running tests..."
ctest --output-on-failure

echo ""
echo "=== Build Complete ==="
echo ""
echo "Build directory: $BUILD_DIR"
echo ""
echo "To run tests manually:"
echo "  cd $BUILD_DIR && ctest --output-on-failure"
echo ""
echo "To build with benchmarks:"
echo "  cmake .. -DBUILD_BENCHMARKS=ON && make -j\$(nproc)"
echo ""
