cmake_minimum_required(VERSION 3.13)
project(crossscp_native VERSION 1.0.0 LANGUAGES C CXX)

# =============================================================================
# crossscp Native Core Library  -  CMake build
# =============================================================================
# Builds libcrossscp.so / .dylib / .dll containing the SFTP client core.
# Links against libssh2 and OpenSSL (or mbedTLS on mobile).
# =============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Prefer LLVM/Clang if available as CMAKE_CXX_COMPILER, but don't force.
# The parent project (Flutter) may use a different compiler.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  find_program(CLANG_CXX clang++)
  if(CLANG_CXX)
    message(STATUS "Using Clang: ${CLANG_CXX}")
    set(CMAKE_C_COMPILER   clang   CACHE STRING "" FORCE)
    set(CMAKE_CXX_COMPILER clang++ CACHE STRING "" FORCE)
  endif()
endif()

# Library version
set(SCP_VERSION "1.0.0-RC1")

# =============================================================================
# Sources
# =============================================================================

set(SCP_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/crossscp/scp_api.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/crossscp/scp_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/crossscp/scp_error.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/crossscp/scp_logger.h
)

set(SCP_PRIVATE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/memory_guard.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ssh_connection.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sftp_session.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sftp_transfer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/connection_pool.h
)

set(SCP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ssh_connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sftp_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sftp_transfer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/connection_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scp_core.cpp
)

# =============================================================================
# Dependencies: libssh2
# =============================================================================

# Try to find system-installed libssh2
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBSSH2 QUIET libssh2)
endif()

if(LIBSSH2_FOUND)
    message(STATUS "Found system libssh2: ${LIBSSH2_LIBRARIES}")
else()
    # Fallback: try find_library
    find_library(LIBSSH2_LIBRARIES NAMES ssh2 libssh2
        PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
              /opt/homebrew/lib /opt/local/lib)
    find_path(LIBSSH2_INCLUDE_DIRS libssh2.h
        PATHS /usr/include /usr/local/include
              /opt/homebrew/include /opt/local/include)

    if(NOT LIBSSH2_LIBRARIES OR NOT LIBSSH2_INCLUDE_DIRS)
        message(FATAL_ERROR "libssh2 not found. Install libssh2-dev (Linux), "
                "libssh2 (macOS Homebrew), or build from source.")
    endif()
    set(LIBSSH2_INCLUDE_DIRS ${LIBSSH2_INCLUDE_DIRS})
endif()

# =============================================================================
# Dependencies: OpenSSL or mbedTLS
# =============================================================================

option(USE_MBEDTLS "Use mbedTLS instead of OpenSSL (for mobile platforms)" OFF)
option(BUILD_SHARED_LIBS_DEFAULT "Build as shared library" ON)

if(USE_MBEDTLS)
    find_package(mbedTLS REQUIRED)
    add_definitions(-DLIBSSH2_MBEDTLS)
    set(CRYPTO_LIBRARIES mbedtls mbedx509 mbedcrypto)
    message(STATUS "Using mbedTLS for crypto")
else()
    find_package(OpenSSL REQUIRED)
    set(CRYPTO_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "Using OpenSSL for crypto (${OPENSSL_VERSION})")
endif()

# =============================================================================
# Target: scp_native shared library
# =============================================================================

add_library(scp_native SHARED
    ${SCP_SOURCES}
    ${SCP_HEADERS}
    ${SCP_PRIVATE_HEADERS}
)

target_include_directories(scp_native
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${LIBSSH2_INCLUDE_DIRS}
)

target_link_libraries(scp_native
    PRIVATE
        ${LIBSSH2_LIBRARIES}
        ${CRYPTO_LIBRARIES}
)

target_compile_definitions(scp_native PRIVATE
    SCP_VERSION="${SCP_VERSION}"
)

# Platform-specific settings
if(WIN32)
    target_link_libraries(scp_native PRIVATE ws2_32)
    target_compile_definitions(scp_native PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(APPLE)
    set_target_properties(scp_native PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        MACOSX_RPATH ON
    )
endif()

# Compiler warnings + optimization
if(MSVC)
    target_compile_options(scp_native PRIVATE /W4 /wd4100 /wd4127 /O2)
else()
    target_compile_options(scp_native PRIVATE
        -O2
        -Wall -Wextra
        -Wno-unused-parameter
    )
endif()

# =============================================================================
# Install
# =============================================================================

include(GNUInstallDirs)
install(TARGETS scp_native
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ${SCP_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/crossscp
)

# =============================================================================
# Print summary
# =============================================================================

message(STATUS "crossscp native library v${SCP_VERSION}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  libssh2:      ${LIBSSH2_LIBRARIES}")
message(STATUS "  Crypto:       ${CRYPTO_LIBRARIES}")
