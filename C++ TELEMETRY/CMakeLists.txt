cmake_minimum_required(VERSION 3.15)
project(TelemetryEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /O2 /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O3 -pthread)
endif()

# Include directories
include_directories(include)

# Executable target
add_executable(TelemetryEngine src/main.cpp)

# Threading library
find_package(Threads REQUIRED)
target_link_libraries(TelemetryEngine PRIVATE Threads::Threads)

