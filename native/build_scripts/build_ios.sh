#!/bin/bash
# =============================================================================
# build_ios.sh  -  Build crossscp native library for iOS/macOS (xcframework)
# =============================================================================
# Usage: ./build_ios.sh [debug|release]
#
# Prerequisites:
#   - Xcode 15+ with command line tools
#   - libssh2 built for iOS (can use Carthage or manual build)
#   - OpenSSL built for iOS (or USE_MBEDTLS=ON)
#
# Output: native/build/ios/crossscp.xcframework/
#   Contains slices for: ios-arm64 (device), ios-arm64-simulator (simulator)
# =============================================================================

set -e

BUILD_TYPE="${1:-release}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NATIVE_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${NATIVE_DIR}/build/ios"
OUTPUT_DIR="${BUILD_DIR}/output"

case "${BUILD_TYPE,,}" in
  release|rel) CONFIG="Release" ;;
  debug|dbg)   CONFIG="Debug" ;;
  *) echo "Unknown build type: $BUILD_TYPE"; exit 1 ;;
esac

echo "=== Building crossscp for iOS/macOS (${CONFIG}) ==="

# Clean previous builds
rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"

# iOS device (arm64)
echo ""
echo "--- Building for ios-arm64 (device) ---"
DEVICE_BUILD="${BUILD_DIR}/ios-device"
cmake "${NATIVE_DIR}" -B "${DEVICE_BUILD}" -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_BUILD_TYPE="${CONFIG}" \
  -DBUILD_SHARED_LIBS_DEFAULT=OFF \
  -DUSE_MBEDTLS=OFF

cmake --build "${DEVICE_BUILD}" --config "${CONFIG}" --target scp_native

# iOS simulator (arm64)
echo ""
echo "--- Building for ios-arm64-simulator ---"
SIM_BUILD="${BUILD_DIR}/ios-simulator"
cmake "${NATIVE_DIR}" -B "${SIM_BUILD}" -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_BUILD_TYPE="${CONFIG}" \
  -DBUILD_SHARED_LIBS_DEFAULT=OFF \
  -DUSE_MBEDTLS=OFF

cmake --build "${SIM_BUILD}" --config "${CONFIG}" --target scp_native

# macOS (arm64 + x86_64)
echo ""
echo "--- Building for macOS ---"
MACOS_BUILD="${BUILD_DIR}/macos"
cmake "${NATIVE_DIR}" -B "${MACOS_BUILD}" -G Xcode \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_BUILD_TYPE="${CONFIG}" \
  -DBUILD_SHARED_LIBS_DEFAULT=OFF \
  -DUSE_MBEDTLS=OFF

cmake --build "${MACOS_BUILD}" --config "${CONFIG}" --target scp_native

# Create XCFramework (iOS only)
echo ""
echo "--- Creating xcframework ---"
XCFRAMEWORK_DIR="${OUTPUT_DIR}/crossscp.xcframework"

xcodebuild -create-xcframework \
  -library "${DEVICE_BUILD}/${CONFIG}/libscp_native.a" \
  -library "${SIM_BUILD}/${CONFIG}/libscp_native.a" \
  -output "${XCFRAMEWORK_DIR}"

echo ""
echo "=== iOS/macOS build complete ==="
echo ""
echo "Output:"
echo "  iOS xcframework: ${XCFRAMEWORK_DIR}"
echo "  macOS library:   ${MACOS_BUILD}/${CONFIG}/libscp_native.a"
echo ""
echo "Flutter integration:"
echo "  1. Drag ${XCFRAMEWORK_DIR} into your Xcode project under ios/Runner/"
echo "  2. Add to ios/Podfile (for Flutter):"
echo "     pod 'crossscp', :podspec => '../native/build/ios/crossscp.podspec'"
echo "  3. Or add directly to Runner target's 'Link Binary with Libraries'"
