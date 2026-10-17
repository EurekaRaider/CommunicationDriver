cmake_minimum_required(VERSION 3.28)

project(
    CommunicationDriver
    VERSION 0.1.2
    LANGUAGES C CXX
)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
    "#include <span>\n#include <stop_token>\n#include <expected>\n#include <coroutine>\nint main(){std::expected<int,int> e=1; std::stop_source s; return *e-1;}"
    CD_HAS_CXX23
)
if(NOT CD_HAS_CXX23)
    message(FATAL_ERROR "C++23 including expected, coroutine, span and stop_token is required")
endif()

option(CD_FETCH_DEPENDENCIES "Download SHA256-locked dependencies" ON)
if(CD_FETCH_DEPENDENCIES)
    include(cmake/Dependencies.cmake)
else()
    find_package(Boost 1.92.0 EXACT CONFIG REQUIRED COMPONENTS process filesystem)
    find_package(re2 CONFIG REQUIRED)
    find_path(LIBSSH2_INCLUDE_DIR libssh2.h REQUIRED)
    find_library(LIBSSH2_LIBRARY NAMES ssh2 libssh2 REQUIRED)
    set(CD_SSH_TARGET ${LIBSSH2_LIBRARY})
endif()

find_package(Threads REQUIRED)
find_package(Iconv)

add_library(
    communicationdriver SHARED
    src/backends/adb/adb_backend.cpp
    src/backends/backend_factory.cpp
    src/backends/cli/cli_backend.cpp
    src/backends/serial/serial_backend.cpp
    src/backends/shared/process_channel.cpp
    src/backends/shared/shell_session.cpp
    src/backends/ssh/ssh_backend.cpp
    src/backends/tcp/tcp_backend.cpp
    src/backends/udp/udp_backend.cpp
    src/c_api/batch.cpp
    src/c_api/checker.cpp
    src/c_api/common.cpp
    src/c_api/connection.cpp
    src/c_api/contracts.cpp
    src/c_api/driver.cpp
    src/c_api/handles.cpp
    src/c_api/operation.cpp
    src/c_api/result.cpp
    src/core/batch.cpp
    src/core/cancellation.cpp
    src/core/connection.cpp
    src/core/operation.cpp
    src/core/options.cpp
    src/core/runtime.cpp
    src/execution/check.cpp
    src/execution/receive_rule.cpp
    src/execution/text_codec.cpp
    src/platform/process.cpp
)
if(APPLE)
    target_sources(communicationdriver PRIVATE src/platform/macos/process_spawn.cpp)
endif()

target_compile_definitions(communicationdriver PRIVATE CD_BUILDING BOOST_ASIO_NO_DEPRECATED)
# Retain cross-module inlining after splitting the former monolithic translation unit.
include(CheckIPOSupported)
check_ipo_supported(RESULT CD_HAS_IPO LANGUAGES CXX)
if(CD_HAS_IPO)
    set_property(TARGET communicationdriver PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
if(UNIX)
    # Children only exec; they never run inherited Asio services. Fork notifications can inherit
    # another runtime's locked signal mutex and deadlock before exec.
    target_compile_definitions(communicationdriver PRIVATE BOOST_PROCESS_V2_DISABLE_NOTIFY_FORK)
endif()

target_include_directories(
    communicationdriver
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${LIBSSH2_INCLUDE_DIR}
)
target_link_libraries(
    communicationdriver PRIVATE Boost::process Boost::filesystem re2::re2 ${CD_SSH_TARGET}
                                Threads::Threads
)
if(Iconv_FOUND)
    target_link_libraries(communicationdriver PRIVATE Iconv::Iconv)
endif()
if(WIN32)
    target_link_libraries(communicationdriver PRIVATE ws2_32)
endif()

set_target_properties(
    communicationdriver
    PROPERTIES OUTPUT_NAME communicationdriver_native
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN YES
               VERSION ${PROJECT_VERSION}
               SOVERSION 1
)
if(MSVC)
    target_compile_options(communicationdriver PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(communicationdriver PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(CD_BUILD_TESTS "Build behavior and ABI tests" ON)
if(CD_BUILD_TESTS)
    enable_testing()

    add_executable(cd_tests tests/native_tests.cpp)
    target_link_libraries(
        cd_tests PRIVATE communicationdriver Boost::process Boost::filesystem Threads::Threads
    )
    add_test(NAME native_behavior COMMAND cd_tests)

    add_executable(cd_cpp_tests tests/cpp_tests.cpp)
    target_link_libraries(cd_cpp_tests PRIVATE communicationdriver)
    add_test(NAME cpp_lifetime COMMAND cd_cpp_tests)

    add_executable(cd_stress_tests tests/stress_tests.cpp)
    target_link_libraries(cd_stress_tests PRIVATE communicationdriver Threads::Threads)
    add_test(NAME native_stress COMMAND cd_stress_tests)
    add_test(NAME native_cancel_close COMMAND cd_stress_tests --soak-seconds 2)
    set_tests_properties(native_stress native_cancel_close PROPERTIES TIMEOUT 60)

    add_executable(cd_integration_tests tests/integration_tests.cpp)
    target_link_libraries(cd_integration_tests PRIVATE communicationdriver)

    add_executable(cd_fake_adb tests/fake_adb.cpp)

    add_executable(cd_adb_hardware_tests tests/adb_hardware_tests.cpp)
    target_link_libraries(cd_adb_hardware_tests PRIVATE communicationdriver Threads::Threads)
    if(MSVC)
        target_compile_options(cd_adb_hardware_tests PRIVATE /utf-8)
    endif()

    add_executable(cd_process_launch_tests tests/process_launch_tests.cpp)

    add_executable(cd_transport_soak_tests tests/transport_soak_tests.cpp)
    target_link_libraries(
        cd_transport_soak_tests PRIVATE communicationdriver Boost::process Boost::filesystem
                                        Threads::Threads
    )
    target_link_libraries(cd_process_launch_tests PRIVATE communicationdriver Threads::Threads)
    add_test(NAME concurrent_process_launch COMMAND cd_process_launch_tests)
    set_tests_properties(concurrent_process_launch PROPERTIES TIMEOUT 30)
    if(NOT WIN32)
        add_test(NAME adb_simulated COMMAND cd_integration_tests --adb $<TARGET_FILE:cd_fake_adb>)
    endif()
endif()

add_executable(cd_c_example examples/c/main.c)
target_link_libraries(cd_c_example PRIVATE communicationdriver)

add_executable(cd_cpp_example examples/cpp/main.cpp)
target_link_libraries(cd_cpp_example PRIVATE communicationdriver)

include(GNUInstallDirs)
install(
    TARGETS communicationdriver
    EXPORT CommunicationDriverTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY skills/communicationdriver
        DESTINATION ${CMAKE_INSTALL_DATADIR}/communicationdriver/skills
)
install(
    EXPORT CommunicationDriverTargets
    NAMESPACE CommunicationDriver::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CommunicationDriver
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/CommunicationDriverConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CommunicationDriverConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CommunicationDriver
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/CommunicationDriverConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CommunicationDriverConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/CommunicationDriverConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CommunicationDriver
)
