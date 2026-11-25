add_library(filescope_his STATIC his.cpp store.cpp)
target_include_directories(filescope_his PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(filescope_his PUBLIC filescope_core PkgConfig::SIGC
                                   PRIVATE PkgConfig::SQLITE3)
filescope_configure_target(filescope_his)
