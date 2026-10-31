cmake_minimum_required(VERSION 3.23)

project(pcp VERSION 1.0.0 LANGUAGES C)

# The example is only built by default when libpcp isn't pulled in by another
# project (e.g. through FetchContent)
option(PCP_BUILD_EXAMPLE "Build the example client" ${PROJECT_IS_TOP_LEVEL})
option(PCP_BUILD_FUZZER "Build the libFuzzer harness for the wire format (clang)"
       OFF)
option(PCP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"
       ${PROJECT_IS_TOP_LEVEL})

include(cmake/warnings.cmake)

set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR ${PCP_WARNINGS_AS_ERRORS})

add_subdirectory(lib)

if(PCP_BUILD_EXAMPLE)
  add_subdirectory(example)
endif()

# BUILD_TESTING comes from CTest. A project pulling in libpcp never builds its
# tests, even if it builds its own
if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
  if(BUILD_TESTING)
    add_subdirectory(test)
  endif()
endif()
