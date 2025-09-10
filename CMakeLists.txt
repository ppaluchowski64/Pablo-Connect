cmake_minimum_required(VERSION 3.20)
project(p_connect LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ENABLE_TESTS ON)

include(FetchContent)

include(cmake/Tests.cmake)
include(cmake/Build.cmake)

find_package(OpenSSL REQUIRED)
find_package(asio    REQUIRED)
find_package(Tracy   REQUIRED)

FetchContent_Declare(
        google-test
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG main
)

FetchContent_Declare(
        google-benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG main
)

FetchContent_Declare(
        debug-log
        GIT_REPOSITORY https://github.com/ddj4747/Debug-Log.git
        GIT_TAG main
)

FetchContent_Declare(
        concurrent_queue-mc
        GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
        GIT_TAG v1.0.4
)

FetchContent_MakeAvailable(google-test)
FetchContent_MakeAvailable(google-benchmark)
FetchContent_MakeAvailable(debug-log)
FetchContent_MakeAvailable(concurrent_queue-mc)

add_library(project_defaults INTERFACE)
target_include_directories(project_defaults INTERFACE
        ${concurrentqueue_SOURCE_DIR}
)
target_compile_definitions(project_defaults INTERFACE
        $<$<CONFIG:Debug>:TRACY_ENABLE>
)

BuildStaticLibrary(network-component-pc utilities/network project_defaults openssl::openssl asio::asio Debug-Log Tracy::TracyClient)
BuildStaticLibrary(system-component-pc utilities/system project_defaults openssl::openssl asio::asio Debug-Log Tracy::TracyClient)
BuildStaticLibrary(p2p-component-pc utilities/p2p project_defaults openssl::openssl asio::asio Debug-Log Tracy::TracyClient network-component-pc system-component-pc)

if (ENABLE_TESTS)
    BuildTestProgram(MultiCast_Test asio-abstractions-programs/MultiCast.cpp network-component-pc system-component-pc p2p-component-pc project_defaults)
    BuildTests(Concurrent_Structures_Test concurrent-structures network-component-pc system-component-pc p2p-component-pc)
    BuildTests(Asio_Abstractions_Test asio-abstractions network-component-pc system-component-pc p2p-component-pc)
endif()