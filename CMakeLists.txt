cmake_minimum_required(VERSION 3.20)
project(MicroDNS VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <new>
    int main() {
        return (int)std::hardware_destructive_interference_size;
    }
" HAS_STD_INTERFERENCE_SIZE)

add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -g
)

add_executable(micro_dns_daemon src/daemon.cpp)
add_executable(micro_dns_client src/client.cpp)

foreach(target micro_dns_daemon micro_dns_client)
  target_link_libraries(${target} PRIVATE project_warnings)
  target_include_directories(${target} PRIVATE include)

  if(HAS_STD_INTERFERENCE_SIZE)
    target_compile_definitions(${target} PRIVATE HAS_STD_INTERFERENCE_SIZE)
  endif()
endforeach()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP true
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(micro_dns_tests
    tests/ring_buffer_test.cpp
    tests/storage_test.cpp
)

target_link_libraries(micro_dns_tests PRIVATE GTest::gtest_main project_warnings)
target_include_directories(micro_dns_tests PRIVATE include)

include(GoogleTest)
gtest_discover_tests(micro_dns_tests)
