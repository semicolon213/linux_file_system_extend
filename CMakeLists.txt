cmake_minimum_required(VERSION 3.21)

project(linux_file_explorer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets)

