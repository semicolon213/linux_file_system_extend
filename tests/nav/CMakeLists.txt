add_executable(test_nav test_nav.c)
target_compile_definitions(test_nav PRIVATE _GNU_SOURCE)
target_link_libraries(test_nav PRIVATE filescope_nav filescope_test_support PkgConfig::GLIB
                                       PkgConfig::GIO)
filescope_configure_target(test_nav)
filescope_add_test(test_nav test_nav)
set_tests_properties(test_nav PROPERTIES LABELS "unit;stress")
