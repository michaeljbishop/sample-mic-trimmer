cmake_minimum_required(VERSION 3.16)

# Qt 6.11 macOS libraries are built for 13.0; must precede project() so every target gets it (macOS hosts only).
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET) # also when an older build dir cached it empty
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "" FORCE)
endif()

project(SampleMicTrimmer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Test)

qt_standard_project_setup()
enable_testing()

add_subdirectory(src/app)
add_subdirectory(src/cli)
add_subdirectory(src/corelib)


add_subdirectory(tests)
