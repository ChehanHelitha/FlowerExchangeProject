cmake_minimum_required(VERSION 3.14)
project(FlowerExchange CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release build: full optimisation + link-time optimisation
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -flto")

# Tell CMake where to find our header files
include_directories(include/Exchange)

add_executable(FlowerExchange
    src/main.cpp
    src/CSVReader.cpp
    src/OrderValidator.cpp
    src/OrderBook.cpp
    src/CSVWriter.cpp
    src/ExchangeApplication.cpp
)

# pthreads required on Linux/macOS for std::thread
find_package(Threads REQUIRED)
target_link_libraries(FlowerExchange PRIVATE Threads::Threads)

# Strict warnings help catch bugs early
target_compile_options(FlowerExchange PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

