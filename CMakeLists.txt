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
    src/store.cpp
)
target_include_directories(kvserver PRIVATE src)
target_link_libraries(kvserver PRIVATE Threads::Threads)