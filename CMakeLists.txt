cmake_minimum_required(VERSION 3.20)

project(rthim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(SDL3TTF REQUIRED IMPORTED_TARGET sdl3-ttf)
pkg_check_modules(MPG123 REQUIRED IMPORTED_TARGET libmpg123)
find_package(ZLIB REQUIRED)

add_executable(rthim
    src/main.cpp
    src/app.cpp
    src/gfx.cpp
    src/menu.cpp
    src/audio.cpp
    src/song.cpp
    src/beatmap.cpp
    src/mapdb.cpp
    src/settings.cpp
    src/settings_ui.cpp
    src/paths.cpp
    src/ui.cpp
    src/screens.cpp
    src/gameplay.cpp
)

target_link_libraries(rthim PRIVATE PkgConfig::SDL3 PkgConfig::SDL3TTF
    PkgConfig::MPG123 ZLIB::ZLIB)
target_compile_options(rthim PRIVATE -Wall -Wextra -Wpedantic)

# Ассеты (шрифты) копируются рядом с бинарником при сборке.
add_custom_command(TARGET rthim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        ${CMAKE_CURRENT_BINARY_DIR}/assets
)