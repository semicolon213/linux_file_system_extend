add_library(filescope_ui STATIC ui.cpp change_panel.cpp)
target_include_directories(filescope_ui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(filescope_ui
  PUBLIC PkgConfig::GTKMM
  PRIVATE filescope_core filescope_job filescope_fs filescope_mon filescope_nav filescope_dash
          filescope_diff filescope_his)
filescope_configure_target(filescope_ui)
