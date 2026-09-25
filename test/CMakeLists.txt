include(FetchContent)
FetchContent_Declare(
  unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG v2.7.0
)
FetchContent_MakeAvailable(unity)

file(GLOB_RECURSE TEST_SOURCES "*.c")

add_executable(tests)

target_sources(tests
  PRIVATE
    ip.c
    lease.c
    main.c
    test_transport.c)

target_link_libraries(tests
  PRIVATE
    pcp
    unity)

target_include_directories(tests PRIVATE
  ${CMAKE_SOURCE_DIR}/lib
  ${CMAKE_SOURCE_DIR}/src)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(tests
    PRIVATE
      -fsanitize=address
      -fno-omit-frame-pointer)
  target_link_options(tests PRIVATE -fsanitize=address)
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(tests PRIVATE /fsanitize=address PRIVATE /Zi)
else()
  message(WARNING "ASan not configured for compiler: %{CMAKE_C_COMPILER_ID")
endif()

add_test(NAME tests COMMAND tests)
