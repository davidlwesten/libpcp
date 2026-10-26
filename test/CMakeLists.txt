include(FetchContent)
FetchContent_Declare(
  unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG v2.7.0
)
FetchContent_MakeAvailable(unity)

add_executable(tests)

target_sources(tests
  PRIVATE
    epoch.c
    ip.c
    runner.c
    schedule.c
    session.c
    wire.c)

target_link_libraries(tests
  PRIVATE
    pcp
    unity)

target_include_directories(tests PRIVATE ${PROJECT_SOURCE_DIR}/lib)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(tests
    PRIVATE
      -fsanitize=address
      -fno-omit-frame-pointer)
  target_link_options(tests PRIVATE -fsanitize=address)
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(tests PRIVATE /fsanitize=address PRIVATE /Zi)
else()
  message(WARNING "ASan not configured for compiler: ${CMAKE_C_COMPILER_ID}")
endif()

# one ctest test per RUN_TEST(...) in the test sources, so tests only need to
# be registered in their file's *_tests() function
get_target_property(TEST_SOURCES tests SOURCES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TEST_SOURCES})

set(TEST_CASES)
foreach(source IN LISTS TEST_SOURCES)
  file(STRINGS ${source} lines REGEX "^ *RUN_TEST\\([a-z0-9_]+\\);")
  foreach(line IN LISTS lines)
    string(REGEX REPLACE "^ *RUN_TEST\\(([a-z0-9_]+)\\);.*" "\\1" test_case "${line}")
    list(APPEND TEST_CASES ${test_case})
  endforeach()
endforeach()

foreach(test_case IN LISTS TEST_CASES)
  add_test(NAME ${test_case} COMMAND tests ${test_case})
endforeach()
