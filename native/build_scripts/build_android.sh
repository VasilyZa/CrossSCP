#!/bin/bash
# =============================================================================
# build_android.sh  -  Build crossscp native library for Android (all ABIs)
# =============================================================================
# Usage:
#   ./build_android.sh [ndk-path]
#
# Prerequisites:
#   - Android NDK r26+ installed (set ANDROID_NDK_HOME or pass as argument)
#   - OpenSSL prebuilt for Android (or build with USE_MBEDTLS=ON)
#
# Output: native/build/android/jniLibs/{arm64-v8a,armeabi-v7a,x86_64}/libcrossscp.so
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NATIVE_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${NATIVE_DIR}/build/android"
OUTPUT_DIR="${NATIVE_DIR}/build/android/output/jniLibs"

# Android NDK location
ANDROID_NDK="${1:-${ANDROID_NDK_HOME}}"
if [ -z "$ANDROID_NDK" ]; then
  # Try common locations
  if [ -d "$HOME/Android/Sdk/ndk" ]; then
    ANDROID_NDK=$(ls -d "$HOME/Android/Sdk/ndk/"*/ | sort -V | tail -1)
  elif [ -d "$ANDROID_SDK_ROOT/ndk" ]; then
    ANDROID_NDK=$(ls -d "$ANDROID_SDK_ROOT/ndk/"*/ | sort -V | tail -1)
  fi
fi

if [ -z "$ANDROID_NDK" ] || [ ! -d "$ANDROID_NDK" ]; then
  echo "ERROR: Android NDK not found."
  echo "Set ANDROID_NDK_HOME environment variable or pass NDK path as argument."
  echo "Example: ./build_android.sh ~/Android/Sdk/ndk/27.0.12077973"
  exit 1
fi

# Remove trailing slash
ANDROID_NDK="${ANDROID_NDK%/}"

echo "=== Building crossscp for Android ==="
echo "NDK: ${ANDROID_NDK}"
echo ""

# Target ABIs
ABIS=("arm64-v8a" "armeabi-v7a" "x86_64")
API_LEVEL=24

# Build libssh2 for each ABI (if not using system packages)
# For simplicity, we assume libssh2 and OpenSSL/mbedTLS are available
# as prebuilt packages or built separately.

export ANDROID_NDK_HOME="${ANDROID_NDK}"
TOOLCHAIN="${ANDROID_NDK}/build/cmake/android.toolchain.cmake"

for ABI in "${ABIS[@]}"; do
  echo ""
  echo "--- Building for ${ABI} ---"

  ABI_BUILD_DIR="${BUILD_DIR}/${ABI}"
  mkdir -p "${ABI_BUILD_DIR}"
  cd "${ABI_BUILD_DIR}"

  cmake "${NATIVE_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DANDROID_ABI="${ABI}" \
    -DANDROID_NATIVE_API_LEVEL="${API_LEVEL}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS_DEFAULT=ON \
    -DUSE_MBEDTLS=OFF \
    -DOPENSSL_ROOT_DIR="${OPENSSL_ANDROID_PREBUILT:-}" \
    -DLIBSSH2_INCLUDE_DIR="${LIBSSH2_ANDROID_PREBUILT:-}/include" \
    -DLIBSSH2_LIBRARY="${LIBSSH2_ANDROID_PREBUILT:-}/lib/${ABI}/libssh2.a"

  cmake --build . --config Release --parallel "$(nproc)"

  # Copy output
  ABI_OUTPUT="${OUTPUT_DIR}/${ABI}"
  mkdir -p "${ABI_OUTPUT}"
  cp -v libscp_native.so "${ABI_OUTPUT}/"
done

echo ""
echo "=== Android build complete ==="
echo "Output: ${OUTPUT_DIR}"
echo ""
echo "To integrate with Flutter:"
echo "  cp -r ${OUTPUT_DIR}/* <flutter_project>/android/app/src/main/jniLibs/"
echo ""
echo "And add to android/app/build.gradle:"
echo "  android {"
echo "    sourceSets {"
echo "      main {"
echo "        jniLibs.srcDirs = ['src/main/jniLibs']"
echo "      }"
echo "    }"
echo "  }"
