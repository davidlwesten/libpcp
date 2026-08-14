include(FetchContent)
FetchContent_Declare(
  unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG v2.7.0
)
FetchContent_MakeAvailable(unity)

file(GLOB_RECURSE TEST_SOURCES "*.c")

add_executable(tests)
target_sources(tests PRIVATE ip.c main.c)
target_link_libraries(tests PRIVATE pcp unity)
add_test(NAME tests COMMAND tests)
