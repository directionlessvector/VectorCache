cmake_minimum_required(VERSION 3.16)
project(kvstore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warnings on — catch mistakes early, especially useful now that
# we have threads/sockets.
add_compile_options(-Wall -Wextra)

# Debug build by default while we're developing. Switch to Release
# later when we benchmark (Day 4).
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Threads REQUIRED)

# Day 1 REPL — still useful for quickly poking at the Store directly.
add_executable(kvstore
    src/main.cpp
    src/store.cpp
)
target_include_directories(kvstore PRIVATE src)

# Day 2 network server — speaks RESP2, thread-per-connection.
add_executable(kvserver
    src/server_main.cpp
    src/server.cpp
    src/resp.cpp
    src/aof.cpp
    src/sharded_store.cpp
    src/store.cpp
)
target_include_directories(kvserver PRIVATE src)
target_link_libraries(kvserver PRIVATE Threads::Threads)

# Benchmark client
add_executable(bench
    bench/load_test.cpp
)
target_include_directories(bench PRIVATE src)
target_link_libraries(bench PRIVATE Threads::Threads)

# In-process benchmark
add_executable(inprocess_bench
    bench/inprocess_bench.cpp
    src/sharded_store.cpp
    src/store.cpp
)
target_include_directories(inprocess_bench PRIVATE src)
target_link_libraries(inprocess_bench PRIVATE Threads::Threads)

# Unit tests
add_executable(sharded_store_test
    tests/sharded_store_test.cpp
    src/sharded_store.cpp
    src/store.cpp
)
target_include_directories(sharded_store_test PRIVATE src)
target_link_libraries(sharded_store_test PRIVATE Threads::Threads)