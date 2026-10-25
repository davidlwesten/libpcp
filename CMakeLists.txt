cmake_minimum_required(VERSION 3.23)

project(pcp VERSION 1.0 LANGUAGES C)

include(cmake/warnings.cmake)

set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

option(BUILD_TESTS "Enable testing" ON)
option(BUILD_EXAMPLE "Enable example" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

add_subdirectory(lib)

if(BUILD_EXAMPLE)
  add_subdirectory(example)
endif()

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()
