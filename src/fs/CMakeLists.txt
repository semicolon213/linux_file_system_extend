add_library(filescope_fs STATIC fs.cpp)
target_include_directories(filescope_fs PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(filescope_fs PUBLIC filescope_core filescope_job PkgConfig::GIOMM)
filescope_configure_target(filescope_fs)
