cmake_minimum_required(VERSION 3.10)
project(zutty VERSION 0.10 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(Freetype REQUIRED)
pkg_check_modules(Xmu REQUIRED IMPORTED_TARGET xmu)
find_package(OpenGL REQUIRED COMPONENTS EGL)
find_package(Threads REQUIRED)

add_executable(zutty
    src/charvdev.cc
    src/font.cc
    src/fontpack.cc
    src/frame.cc
    src/gl.cc
    src/log.cc
    src/main.cc
    src/options.cc
    src/pty.cc
    src/renderer.cc
    src/selmgr.cc
    src/vterm.cc
)
target_compile_features(zutty PRIVATE cxx_std_14)
target_compile_definitions(zutty PRIVATE ZUTTY_VERSION=\"${PROJECT_VERSION}\")
target_link_libraries(zutty PRIVATE Fontconfig::Fontconfig Freetype::Freetype PkgConfig::Xmu OpenGL::EGL Threads::Threads)

