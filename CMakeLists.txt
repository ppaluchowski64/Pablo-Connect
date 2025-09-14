cmake_minimum_required(VERSION 3.20)
project(p_connect LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ENABLE_TESTS ON)

include(FetchContent)

include(cmake/Tests.cmake)
include(cmake/Build.cmake)

find_package(OpenSSL         REQUIRED)
find_package(asio            REQUIRED)
find_package(Tracy           REQUIRED)
find_package(concurrentqueue REQUIRED)
find_package(GTest           REQUIRED)
find_package(benchmark       REQUIRED)

FetchContent_Declare(
        debug-log
        GIT_REPOSITORY https://github.com/ddj4747/Debug-Log.git
        GIT_TAG main
)

FetchContent_MakeAvailable(debug-log)

add_library(project_defaults INTERFACE)

target_compile_definitions(project_defaults INTERFACE
        $<$<CONFIG:Debug>:TRACY_ENABLE>
)

BuildStaticLibrary(network-component-pc utilities/network project_defaults openssl::openssl asio::asio Debug-Log Tracy::TracyClient concurrentqueue::concurrentqueue)
BuildStaticLibrary(system-component-pc utilities/system project_defaults openssl::openssl asio::asio Debug-Log Tracy::TracyClient concurrentqueue::concurrentqueue )
BuildStaticLibrary(p2p-component-pc utilities/p2p project_defaults openssl::openssl asio::asio Debug-Log Tracy::TracyClient network-component-pc system-component-pc concurrentqueue::concurrentqueue)
BuildStaticLibrary(device-scanner-pc utilities/device-scanner project_defaults network-component-pc system-component-pc)

if (ENABLE_TESTS)
    BuildTestProgram(MultiCast_Test asio-abstractions-programs/MultiCast.cpp device-scanner-pc)
    BuildTests(Concurrent_Structures_Test concurrent-structures network-component-pc system-component-pc p2p-component-pc)
    BuildTests(Asio_Abstractions_Test asio-abstractions network-component-pc system-component-pc p2p-component-pc)
endif()