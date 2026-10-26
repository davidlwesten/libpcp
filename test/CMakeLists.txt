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
  test_epoch_first_response_valid
  test_epoch_steady_clocks_valid
  test_epoch_slack_valid
  test_epoch_drift_valid
  test_epoch_server_went_backwards
  test_epoch_server_too_slow
  test_epoch_server_too_fast
  test_epoch_stored_even_if_invalid
  test_schedule_due_at_start
  test_schedule_renewal_first_window
  test_schedule_renewal_later_windows
  test_schedule_renewal_at_least_4s_apart
  test_schedule_renewal_exhausted_at_expiry
  test_schedule_defer
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
  test_session_send_writes_into_config_buffer
  test_session_retransmits_on_schedule
  test_session_jitter_applied
  test_session_timeout_is_earliest_mapping
  test_session_wakes_at_deadlines
  test_session_server_reset_requests_again
  test_session_success_maps
  test_session_renews_mapping
  test_session_renewal_keeps_address
  test_session_renewal_changes_address
  test_session_renewal_temporary_error_keeps_mapping
  test_session_renewal_refused_fails
  test_session_mapping_expires_without_renewal
  test_session_temporary_error_retries_later
  test_session_permanent_error_fails
  test_session_unknown_result_fails
  test_session_response_matches_its_mapping_only
  test_session_peer_response_must_match_remote_peer
  test_session_duplicate_response_ignored
  test_session_suggests_any_ipv4_address
  test_session_suggests_any_ipv6_address
  test_session_suggests_granted_address_afterwards
  test_session_server_reset_unmaps_active_mapping
  test_session_server_reset_unmaps_once
  test_session_server_reset_pending_mapping_not_unmapped
  test_session_server_reset_before_mapped_event
  test_session_response_revealing_reset_not_applied
  test_session_announce_consistent_epoch_ignored
  test_session_announce_reset_unmaps_then_waits
  test_session_announce_reset_delays_pending_mapping
  test_session_announce_error_still_resets
  test_session_ignores_malformed_datagrams
  test_wire_map_request
  test_wire_map_request_without_suggestion
  test_wire_peer_request
  test_wire_read_map_response
  test_wire_read_peer_response
  test_wire_read_announce_response
  test_wire_read_announce_response_skips_options
  test_wire_read_error_response
  test_wire_read_response_skips_options
  test_wire_read_response_rejects_malformed)

foreach(test_case IN LISTS TEST_CASES)
  add_test(NAME ${test_case} COMMAND tests ${test_case})
endforeach()
