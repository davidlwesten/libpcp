add_library(project_warnings INTERFACE)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
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
  message(
    WARNING
    "No known warning flags set for compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

