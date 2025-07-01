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

FetchContent_MakeAvailable(google-test)
FetchContent_MakeAvailable(google-benchmark)
FetchContent_MakeAvailable(debug-log)
FetchContent_MakeAvailable(thread-safe-structs)

add_executable(head_pconnect main.cpp)
target_link_libraries(head_pconnect PRIVATE openssl::openssl asio::asio Debug-Log custom_containers)