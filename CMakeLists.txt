cmake_minimum_required(VERSION 3.20)
project(chronos LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# maya as a submodule — build the library, skip its examples/tests
set(MAYA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MAYA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
add_subdirectory(maya)

add_executable(chronos
    src/main.cpp
)
target_include_directories(chronos PRIVATE src)
target_link_libraries(chronos PRIVATE maya)
target_compile_options(chronos PRIVATE -O2)

install(TARGETS chronos RUNTIME DESTINATION bin)
