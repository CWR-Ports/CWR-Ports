cmake_minimum_required(VERSION 3.29)
project(etcpak)
add_library(etcpak STATIC ProcessRGB.cpp Tables.cpp Dither.cpp Error.cpp)
target_include_directories(etcpak PUBLIC .)
if(ANDROID AND (CMAKE_ANDROID_ARCH_ABI MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64"))
    target_compile_definitions(etcpak PRIVATE __ARM_NEON)
endif()

# Compile as C++17 to match engine
set_target_properties(etcpak PROPERTIES CXX_STANDARD 17)
