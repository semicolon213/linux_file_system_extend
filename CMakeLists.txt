cmake_minimum_required(VERSION 3.21)

project(filescope VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)

add_library(filescope_info STATIC src/app_info.c)
target_include_directories(filescope_info PUBLIC src)
target_compile_definitions(filescope_info PRIVATE FILESCOPE_VERSION="${PROJECT_VERSION}")

add_executable(filescope src/main.c)
target_link_libraries(filescope PRIVATE filescope_info PkgConfig::GTK3)

function(filescope_configure_target target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(FILESCOPE_SANITIZER)
    target_compile_options(${target} PRIVATE -fsanitize=${FILESCOPE_SANITIZER} -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=${FILESCOPE_SANITIZER})
  endif()
endfunction()

filescope_configure_target(filescope_info)
filescope_configure_target(filescope)

include(CTest)
if(BUILD_TESTING)
  add_executable(filescope_tests tests/test_app_info.c)
  target_link_libraries(filescope_tests PRIVATE filescope_info PkgConfig::GLIB)
  filescope_configure_target(filescope_tests)
  if(FILESCOPE_SANITIZER STREQUAL "thread")
    find_program(SETARCH_EXECUTABLE setarch REQUIRED)
    add_test(NAME app_info COMMAND ${SETARCH_EXECUTABLE} ${CMAKE_SYSTEM_PROCESSOR} -R $<TARGET_FILE:filescope_tests>)
  else()
    add_test(NAME app_info COMMAND filescope_tests)
  endif()
endif()

