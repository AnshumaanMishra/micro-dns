cmake_minimum_required(VERSION 3.20)
project(MicroDNS VERSION 1.0)

# Force C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Compiler flags for strict warnings, TSAN, and debugging symbols
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic \
      -Wno-interference-size -g")

# Create our server executable
add_executable(micro_dns_daemon src/daemon.cpp)
add_executable(micro_dns_cli src/client.cpp)
