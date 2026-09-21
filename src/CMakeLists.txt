add_executable(pcpc)

target_compile_features(pcpc PUBLIC c_std_23)
target_compile_definitions(pcpc PRIVATE _POSIX_C_SOURCE=200112L)

target_sources(pcpc PRIVATE
  main.c
  socket.c
  transport_socket.c)
target_include_directories(pcpc PRIVATE ${CMAKE_SOURCE_DIR}/lib ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pcpc
  PUBLIC
    pcp
  PRIVATE
    project_warnings)
