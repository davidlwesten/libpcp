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
    runner.c
    schedule.c
    session.c
    wire.c)

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

set(TEST_CASES
  test_ip_address_from_in_addr
  test_ip_address_from_in6_addr
  test_ip_address_ntop_ipv4
  test_ip_address_ntop_ipv6
  test_ip_address_constants
  test_socket_address_ntop_ipv4
  test_socket_address_ntop_ipv6
  test_schedule_due_at_start
  test_schedule_backoff_without_jitter
  test_schedule_backoff_capped_at_mrt
  test_schedule_jitter_bounds
  test_schedule_jitter_wraps_random
  test_schedule_no_jitter_when_rng_fails
  test_schedule_next_relative_to_transmission
  test_schedule_exhausted_by_mrc
  test_schedule_exhausted_by_mrd
  test_schedule_mrd_from_first_transmission
  test_schedule_restart
  test_session_never_without_mappings
  test_session_new_request_due_immediately
  test_session_no_send_before_due
  test_session_send_event_carries_handle
  test_session_send_writes_into_config_buffer
  test_session_retransmits_on_schedule
  test_session_jitter_applied
  test_session_timeout_is_earliest_mapping
  test_session_wakes_at_deadlines
  test_wire_map_request
  test_wire_map_request_without_suggestion
  test_wire_peer_request)

foreach(test_case IN LISTS TEST_CASES)
  add_test(NAME ${test_case} COMMAND tests ${test_case})
endforeach()
