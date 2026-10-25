add_library(pcp)

target_compile_features(pcp PUBLIC c_std_23)

target_compile_definitions(pcp
  PRIVATE
    _POSIX_C_SOURCE=200112L)

target_sources(pcp
  PRIVATE
    epoch.c
    ip.c
    mapping.c
    pcp.c
    schedule.c
    session.c
    wire.c)

target_sources(pcp
  PUBLIC
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_SOURCE_DIR}/include
    FILES
      ${CMAKE_SOURCE_DIR}/include/pcp/ip.h
      ${CMAKE_SOURCE_DIR}/include/pcp/pcp.h)

target_sources(pcp
  PRIVATE
    FILE_SET private_headers
      TYPE HEADERS
      BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
      FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/epoch.h
        ${CMAKE_CURRENT_SOURCE_DIR}/mapping.h
        ${CMAKE_CURRENT_SOURCE_DIR}/nonce.h
        ${CMAKE_CURRENT_SOURCE_DIR}/request.h
        ${CMAKE_CURRENT_SOURCE_DIR}/response.h
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule.h
        ${CMAKE_CURRENT_SOURCE_DIR}/session.h
        ${CMAKE_CURRENT_SOURCE_DIR}/wire.h)

target_link_libraries(pcp PRIVATE project_warnings)
