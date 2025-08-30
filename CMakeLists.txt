cmake_minimum_required(VERSION 3.20)
project(p_connect LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ENABLE_TESTS ON)

include(FetchContent)
include(cmake/Tests.cmake)

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

file(GLOB_RECURSE UTIL_SRC utilities/*)

add_library(util_lib_p_connect STATIC ${UTIL_SRC})
target_link_libraries(util_lib_p_connect PUBLIC openssl::openssl asio::asio Debug-Log Tracy::TracyClient)

target_compile_definitions(util_lib_p_connect PUBLIC
        $<$<CONFIG:Debug>:TRACY_ENABLE>
)

target_include_directories(util_lib_p_connect PUBLIC utilities/inc/ ${concurrentqueue_SOURCE_DIR})

if (ENABLE_TESTS)
    BuildTestProgram(MultiCast_Test asio-abstractions-programs/MultiCast.cpp util_lib_p_connect)
    BuildTests(Asio_Abstractions_Test asio-abstractions util_lib_p_connect)
    BuildTests(Concurrent_Structures_Test concurrent-structures util_lib_p_connect)
endif()