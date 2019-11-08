# CMakeLists.txt for MoonClockExample
#
# © 2018 by Richard Walters

cmake_minimum_required(VERSION 3.8)
set(This MoonClockExample)

set(Sources
    src/main.cpp
)

add_executable(${This} ${Sources})
set_target_properties(${This} PROPERTIES
    FOLDER Applications
)

target_include_directories(${This} PRIVATE ..)

target_link_libraries(${This} PUBLIC
    MoonClock
    SystemAbstractions
)

if(UNIX AND NOT APPLE)
    target_link_libraries(${This} PRIVATE
        -static-libstdc++
    )
endif(UNIX AND NOT APPLE)
