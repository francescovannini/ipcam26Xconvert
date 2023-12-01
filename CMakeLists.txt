cmake_minimum_required(VERSION 3.10)
project(ipcam26Xconvert C)

set(CMAKE_C_STANDARD 99)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)

add_executable(ipcam264convert main.c ipcamvideofilefmt.h)
target_link_libraries(ipcam264convert PkgConfig::LIBAV m)
target_compile_options(ipcam264convert PRIVATE -Wall -Wno-deprecated-declarations)

