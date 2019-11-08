# CMakeLists.txt for MoonClock
#
# © 2019 by Richard Walters

cmake_minimum_required(VERSION 3.8)
set(This MoonClock)

set(Headers
    include/MoonClock/MoonClock.hpp
)

set(Sources
    src/MoonClock.cpp
)

add_library(${This} STATIC ${Sources} ${Headers})
set_target_properties(${This} PROPERTIES
    FOLDER Libraries
)

target_include_directories(${This} PUBLIC include)

target_link_libraries(${This} PUBLIC
    LuaLibrary
    SystemAbstractions
    Timekeeping
)

add_subdirectory(example)
add_subdirectory(test)
