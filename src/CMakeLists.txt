cmake_minimum_required(VERSION 4.2)
project(MusicGraph C)

set(CMAKE_C_STANDARD 11)

add_executable(MusicGraph WIN32 main.c resource.rc)

target_link_libraries(MusicGraph PRIVATE opengl32 ole32 shell32 user32 gdi32)

