add_executable(pcpc)

target_compile_features(pcpc PUBLIC c_std_23)

target_sources(pcpc PRIVATE main.c)
target_link_libraries(pcpc
  PUBLIC
    pcp
  PRIVATE
    project_warnings)

