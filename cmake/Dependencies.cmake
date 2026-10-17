include(FetchContent)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS
    OFF
    CACHE BOOL "Static third-party dependencies" FORCE
)
set(BUILD_TESTING
    OFF
    CACHE BOOL "Do not build dependency test suites" FORCE
)

set(BOOST_INCLUDE_LIBRARIES asio process filesystem)
set(BOOST_ENABLE_CMAKE ON)
FetchContent_Declare(
    boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.92.0/boost-1.92.0-cmake.tar.xz
    URL_HASH SHA256=9bed76128d4e46755dbe818487788c6fceb6f72b378f4daa49b7e1e600d9088d
)

set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_ENABLE_INSTALL ON)
if(MSVC)
    set(ABSL_MSVC_STATIC_RUNTIME
        ON
        CACHE BOOL "Match the native static CRT" FORCE
    )
endif()
FetchContent_Declare(
    absl
    URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20250512.1.tar.gz
    URL_HASH SHA256=9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db
    OVERRIDE_FIND_PACKAGE
)

set(RE2_BUILD_TESTING OFF)
FetchContent_Declare(
    re2
    URL https://github.com/google/re2/archive/refs/tags/2025-11-05.tar.gz
    URL_HASH SHA256=87f6029d2f6de8aa023654240a03ada90e876ce9a4676e258dd01ea4c26ffd67
)

set(BUILD_EXAMPLES
    OFF
    CACHE BOOL "Dependency examples" FORCE
)
set(BUILD_STATIC_LIBS
    ON
    CACHE BOOL "libssh2 static" FORCE
)
set(ENABLE_DEBUG_LOGGING
    OFF
    CACHE BOOL "No SSH debug logging" FORCE
)
if(WIN32)
    set(CRYPTO_BACKEND
        WinCNG
        CACHE STRING "SSH cryptography" FORCE
    )
else()
    set(CRYPTO_BACKEND
        OpenSSL
        CACHE STRING "SSH cryptography" FORCE
    )
endif()
FetchContent_Declare(
    ssh2
    URL https://www.libssh2.org/download/libssh2-1.11.1.tar.gz
    URL_HASH SHA256=d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7
)

FetchContent_MakeAvailable(boost absl re2 ssh2)

include(${CMAKE_CURRENT_LIST_DIR}/PatchLibssh2.cmake)
cd_patch_libssh2("${ssh2_SOURCE_DIR}")
set(CD_SSH_TARGET libssh2_static)
