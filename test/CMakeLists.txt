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
    allocator.c
    lease.c
    transport.c
    runner.c)

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
  test_custom_allocator_lifecycle
  test_arena_allocator_alignment_and_exhaustion
  test_arena_allocator_client_lifecycle
  test_socket_address_ntop_ipv4
  test_socket_address_ntop_ipv6
  test_pending_activation_and_renewal
  test_shutdown_during_renewal_deletes_mapping
  test_shutdown_waits_for_deletion_response
  test_shutdown_finishes_when_response_deadline_expires
  test_shutdown_waits_for_all_send_completions
  test_shutdown_reports_send_error
  test_shutdown_reports_timer_creation_error_in_callback
  test_transient_and_permanent_response_errors
  test_test_transport_send_and_receive
  test_test_transport_timers)

foreach(test_case IN LISTS TEST_CASES)
  add_test(NAME ${test_case} COMMAND tests ${test_case})
endforeach()
