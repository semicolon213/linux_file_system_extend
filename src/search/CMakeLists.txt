add_library(filescope_search STATIC search.cpp)
target_include_directories(filescope_search PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(filescope_search PUBLIC filescope_core PkgConfig::GIOMM PkgConfig::SIGC)
filescope_configure_target(filescope_search)
