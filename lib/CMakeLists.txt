add_library(pcp)

target_compile_features(pcp PUBLIC c_std_23)

target_compile_definitions(pcp
  PRIVATE
    _POSIX_C_SOURCE=200112L)

target_sources(pcp
  PRIVATE
    allocator.c
    client.c
    ip.c
    lease.c
    map.c
    option.c
    packet.c
    peer.c
    result.c
    server.c)

# Public headers
target_sources(pcp
  PUBLIC
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_SOURCE_DIR}/include
    FILES
      ${CMAKE_SOURCE_DIR}/include/pcp/ip.h
      ${CMAKE_SOURCE_DIR}/include/pcp/allocator.h
      ${CMAKE_SOURCE_DIR}/include/pcp/option.h
      ${CMAKE_SOURCE_DIR}/include/pcp/pcp.h
      ${CMAKE_SOURCE_DIR}/include/pcp/protocol.h
      ${CMAKE_SOURCE_DIR}/include/pcp/result.h
      ${CMAKE_SOURCE_DIR}/include/pcp/transport.h)

# Private headers
target_sources(pcp
  PRIVATE
    FILE_SET private_headers
      TYPE HEADERS
      BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
      FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/client.h
        ${CMAKE_CURRENT_SOURCE_DIR}/lease.h
        ${CMAKE_CURRENT_SOURCE_DIR}/map.h
        ${CMAKE_CURRENT_SOURCE_DIR}/packet.h
        ${CMAKE_CURRENT_SOURCE_DIR}/peer.h
        ${CMAKE_CURRENT_SOURCE_DIR}/server.h
        ${CMAKE_CURRENT_SOURCE_DIR}/types.h)

find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
  target_link_libraries(pcp PRIVATE ${MATH_LIBRARY})
endif()

target_link_libraries(pcp PRIVATE project_warnings)
