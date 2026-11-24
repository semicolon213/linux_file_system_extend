add_library(filescope_dash STATIC dash.cpp)
target_include_directories(filescope_dash PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(filescope_dash PUBLIC filescope_core)
filescope_configure_target(filescope_dash)
