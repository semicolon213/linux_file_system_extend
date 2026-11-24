set(core_test_sources
    test_core_error.cpp
    test_core_event.cpp
    test_core_fd.cpp
    test_core_path.cpp
    test_core_types.cpp)

foreach(source IN LISTS core_test_sources)
  get_filename_component(target ${source} NAME_WE)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE filescope_core filescope_test_support)
  filescope_configure_target(${target})
  filescope_add_test(${target} ${target})
  set_tests_properties(${target} PROPERTIES LABELS unit)
endforeach()
