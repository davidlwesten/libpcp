cmake_minimum_required(VERSION 3.23)

project(pcp VERSION 1.0 LANGUAGES C)

add_library(project_warnings INTERFACE)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(project_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2)
elseif(CMAKE_C_COMPILER_ID MATCHES "MSVC")
  target_compile_options(project_warnings INTERFACE
    /W4
    /permissive-)
else()
  message(WARNING "No known warning flags set for compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

option(BUILD_TESTS "Enable testing" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

add_subdirectory(lib)
add_subdirectory(src)

if (BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()
