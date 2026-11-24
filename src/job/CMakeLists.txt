add_library(filescope_job STATIC fs_job.c)
target_include_directories(filescope_job PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(filescope_job PUBLIC filescope_core PkgConfig::GLIB PkgConfig::GIO)
filescope_configure_target(filescope_job)
