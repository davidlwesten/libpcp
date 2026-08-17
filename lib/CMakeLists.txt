add_library(pcp)

target_compile_features(pcp PUBLIC c_std_23)

target_compile_definitions(pcp
  PRIVATE
    _POSIX_C_SOURCE=200112L)

target_sources(pcp
  PRIVATE
    datagram.c
    ip.c
    map.c
    option.c
    pcp.c
    peer.c
    result.c
    socket.c
    transport_socket.c)

# Public headers
target_sources(pcp
  PUBLIC
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_SOURCE_DIR}/include
    FILES
      ${CMAKE_SOURCE_DIR}/include/pcp/ip.h
      ${CMAKE_SOURCE_DIR}/include/pcp/option.h
      ${CMAKE_SOURCE_DIR}/include/pcp/pcp.h
      ${CMAKE_SOURCE_DIR}/include/pcp/protocol.h
      ${CMAKE_SOURCE_DIR}/include/pcp/result.h)

# Private headers
target_sources(pcp
  PRIVATE
    FILE_SET private_headers
      TYPE HEADERS
      BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
      FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/datagram.h
        ${CMAKE_CURRENT_SOURCE_DIR}/internal.h
        ${CMAKE_CURRENT_SOURCE_DIR}/map.h
        ${CMAKE_CURRENT_SOURCE_DIR}/peer.h
        ${CMAKE_CURRENT_SOURCE_DIR}/socket.h
        ${CMAKE_CURRENT_SOURCE_DIR}/transport.h)

target_link_libraries(pcp PRIVATE project_warnings)
