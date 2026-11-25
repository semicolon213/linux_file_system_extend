add_library(filescope_diff STATIC diff.cpp)
target_include_directories(filescope_diff PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(filescope_diff PUBLIC filescope_core PRIVATE PkgConfig::GLIB)
filescope_configure_target(filescope_diff)
