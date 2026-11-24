add_library(filescope_job STATIC job.cpp)
target_include_directories(filescope_job PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
  filescope_job
  PUBLIC filescope_core PkgConfig::GLIBMM PkgConfig::GIOMM PkgConfig::SIGC
  PRIVATE PkgConfig::GLIB)
filescope_configure_target(filescope_job)
