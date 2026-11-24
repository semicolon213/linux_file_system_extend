set(core_test_sources
    test_core_architecture.c
    test_core_error.c
    test_core_event.c
    test_core_path.c
    test_core_types.c)

foreach(source IN LISTS core_test_sources)
  get_filename_component(target ${source} NAME_WE)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE filescope_core filescope_test_support PkgConfig::GLIB)
  filescope_configure_target(${target})
  filescope_add_test(${target} ${target})
  set_tests_properties(${target} PROPERTIES LABELS unit)
endforeach()

target_compile_definitions(test_core_architecture
                           PRIVATE FILESCOPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
