add_library(filescope_trash STATIC trash.cpp)
target_include_directories(filescope_trash PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(filescope_trash PUBLIC filescope_core filescope_job filescope_fs PkgConfig::GIOMM PkgConfig::GIO PkgConfig::SIGC)
filescope_configure_target(filescope_trash)
