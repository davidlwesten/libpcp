add_library(pcp)
add_library(pcp::pcp ALIAS pcp)

target_compile_features(pcp PUBLIC c_std_23)

target_compile_definitions(pcp
  PRIVATE
    _POSIX_C_SOURCE=200112L)

target_sources(pcp
  PRIVATE
    epoch.c
    ip.c
    mapping.c
    schedule.c
    session.c
    wire.c
    pcp_gen.c)

# The wire format is generated from pcp.pkt by pktc
# (https://github.com/davidlwesten/pktc). The output is checked in, so pktc is
# only needed to change the schema. When it's installed, editing pcp.pkt
# regenerates the code as part of the build, along with pktc's libFuzzer
# harness for it (test/pcp_fuzz.c).
find_program(PKTC_EXECUTABLE pktc)
if(PKTC_EXECUTABLE)
  # pktc writes everything into one directory, so generate into the build tree
  # and copy the harness to the tests
  set(PKTC_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pktc)
  add_custom_command(
    OUTPUT
      ${CMAKE_CURRENT_SOURCE_DIR}/pcp_gen.c
      ${CMAKE_CURRENT_SOURCE_DIR}/pcp_gen.h
      ${PROJECT_SOURCE_DIR}/test/pcp_fuzz.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PKTC_OUTPUT}
    COMMAND ${PKTC_EXECUTABLE} --fuzz -o ${PKTC_OUTPUT}
            ${CMAKE_CURRENT_SOURCE_DIR}/pcp.pkt
    COMMAND ${CMAKE_COMMAND} -E copy ${PKTC_OUTPUT}/pcp_gen.c
            ${PKTC_OUTPUT}/pcp_gen.h ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${PKTC_OUTPUT}/pcp_fuzz.c
            ${PROJECT_SOURCE_DIR}/test
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/pcp.pkt
    COMMENT "Generating the PCP wire format from pcp.pkt"
    VERBATIM)
endif()

target_sources(pcp
  PUBLIC
  FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    FILES
      ${PROJECT_SOURCE_DIR}/include/pcp/ip.h
      ${PROJECT_SOURCE_DIR}/include/pcp/pcp.h)

target_sources(pcp
  PRIVATE
    FILE_SET private_headers
      TYPE HEADERS
      BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
      FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/epoch.h
        ${CMAKE_CURRENT_SOURCE_DIR}/mapping.h
        ${CMAKE_CURRENT_SOURCE_DIR}/pcp_gen.h
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule.h
        ${CMAKE_CURRENT_SOURCE_DIR}/session.h
        ${CMAKE_CURRENT_SOURCE_DIR}/wire.h)

target_link_libraries(pcp PRIVATE pcp_warnings)
