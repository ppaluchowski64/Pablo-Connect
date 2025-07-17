cmake_minimum_required(VERSION 3.20)
project(head_pconnect LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

find_package(OpenSSL REQUIRED)
find_package(asio    REQUIRED)

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
        thread-safe-structs
        GIT_REPOSITORY https://github.com/ddj4747/Thread-safe-structs.git
        GIT_TAG main
)

FetchContent_Declare(
        concurrentqueue-mc
        GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
        GIT_TAG v1.0.4
)

FetchContent_MakeAvailable(google-test)
FetchContent_MakeAvailable(google-benchmark)
FetchContent_MakeAvailable(debug-log)
FetchContent_MakeAvailable(thread-safe-structs)
FetchContent_MakeAvailable(concurrentqueue-mc)

file(GLOB_RECURSE UTIL_SRC utilities/*)
add_library(util_lib_pconnect STATIC ${UTIL_SRC})
target_link_libraries(util_lib_pconnect PRIVATE openssl::openssl asio::asio Debug-Log custom_containers)
target_include_directories(util_lib_pconnect PUBLIC utilities/inc/ ${concurrentqueue_SOURCE_DIR})

add_executable(pawelek_test tests/pawelek_test.cpp)
target_link_libraries(pawelek_test PRIVATE openssl::openssl asio::asio Debug-Log custom_containers util_lib_pconnect)

add_executable(temp_test tests/temp.cpp)
target_link_libraries(temp_test PRIVATE openssl::openssl asio::asio Debug-Log custom_containers util_lib_pconnect)