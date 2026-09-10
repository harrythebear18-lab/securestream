cmake_minimum_required(VERSION 3.20)
project(SecureStream VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC-specific: use UTF-8, disable permissive mode warnings
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /WX-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_subdirectory(compositor)
