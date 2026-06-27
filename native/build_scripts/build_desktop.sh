#!/bin/bash
# =============================================================================
# build_desktop.sh  -  Build the crossscp native library for desktop platforms
# =============================================================================
# Usage: ./build_desktop.sh [debug|release] [clean]
#
# Prerequisites:
#   Linux:   sudo apt install libssh2-1-dev libssl-dev cmake build-essential
#   macOS:   brew install libssh2 openssl cmake
#   Windows: Install vcpkg + libssh2, or use MSYS2/cygwin
# =============================================================================

set -e

BUILD_TYPE="${1:-release}"
CLEAN="${2:-}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NATIVE_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${NATIVE_DIR}/build/desktop"

# Normalize build type
case "${BUILD_TYPE,,}" in
  release|rel) CMAKE_BUILD_TYPE="Release" ;;
  debug|dbg)   CMAKE_BUILD_TYPE="Debug" ;;
  *) echo "Unknown build type: $BUILD_TYPE (use debug or release)"; exit 1 ;;
esac

echo "=== Building crossscp native library (${CMAKE_BUILD_TYPE}) ==="

# Clean if requested
if [ "$CLEAN" = "clean" ]; then
  echo "Cleaning build directory..."
  rm -rf "${BUILD_DIR}"
fi

# Configure
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${NATIVE_DIR}" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
  -DBUILD_SHARED_LIBS_DEFAULT=ON \
  -DUSE_MBEDTLS=OFF

# Build
cmake --build . --config "${CMAKE_BUILD_TYPE}" --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo ""
echo "=== Build complete ==="
echo "Output directory: ${BUILD_DIR}"
echo ""
echo "Linux output:   libscp_native.so"
echo "macOS output:   libscp_native.dylib"
echo "Windows output: scp_native.dll"

# Copy to Flutter-compatible locations (optional)
if [ -f "libscp_native.so" ]; then
  cp -v libscp_native.so "${NATIVE_DIR}/../../linux/" 2>/dev/null || true
elif [ -f "libscp_native.dylib" ]; then
  cp -v libscp_native.dylib "${NATIVE_DIR}/../../macos/" 2>/dev/null || true
elif [ -f "scp_native.dll" ]; then
  cp -v scp_native.dll "${NATIVE_DIR}/../../windows/" 2>/dev/null || true
fi

echo "Done."
