cmake_minimum_required(VERSION 3.20.0)
set(CONF_FILE prj.conf ../lib/common.conf)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(esp32_wifi)


FILE(GLOB app_sources src/*.c)
FILE(GLOB lib_sources ../lib/*.c)
target_sources(app PRIVATE ${app_sources} ${lib_sources})
target_link_libraries(app PRIVATE)
target_include_directories(app PRIVATE ../lib)

add_compile_options(-Wno-gcc-extension -Wno-extension)