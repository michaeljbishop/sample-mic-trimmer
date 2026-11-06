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

# Precompiled headers, two sets. Core: the most used QtCore headers, built once by an otherwise empty library and
# reused (REUSE_FROM) by corelib, the command line tool and every test. Gui: the same plus QWidget, the app's own.
set(SMT_PCH_CORE <QObject> <QString> <QStringList> <QList> <QHash> <QSet> <QDir> <QFile> <QFileInfo> <QJsonObject>)
set(SMT_PCH_GUI ${SMT_PCH_CORE} <QWidget>)
file(WRITE ${CMAKE_BINARY_DIR}/smt_pch_core.cpp "// owner of the shared QtCore precompiled header\n")
add_library(smt_pch_core STATIC ${CMAKE_BINARY_DIR}/smt_pch_core.cpp)
set_target_properties(smt_pch_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(smt_pch_core PRIVATE Qt6::Core)
target_precompile_headers(smt_pch_core PRIVATE ${SMT_PCH_CORE})

add_subdirectory(src/app)
add_subdirectory(src/cli)
add_subdirectory(src/corelib)


add_subdirectory(tests)
