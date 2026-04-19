cmake_minimum_required(VERSION 3.20.0)
set(CONF_FILE prj.conf ../lib/common.conf)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(esp32_wifi)


set(APP_SOURCES
    src/main.c
    src/moisture.c
    src/room.c
)

set(LIB_SOURCES
    ../lib/http_get.c
    ../lib/mqtt.c
    ../lib/ping.c
    ../lib/wifi.c
)

target_sources(app PRIVATE ${APP_SOURCES} ${LIB_SOURCES})
target_include_directories(app PRIVATE ../lib)

add_compile_options(-Wno-gcc-extension -Wno-extension)