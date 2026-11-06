qt_add_library(corelib STATIC
    corelib.cpp
    corelib.h
    coreutil.h
    workrelay.h
    smt.h smt.cpp
    librarydb.h librarydb.cpp
    projectconfig.h projectconfig.cpp
    profile.h profile.cpp
    profileregistry.h profileregistry.cpp
    pathsafety.h pathsafety_testing.h pathsafety.cpp
    plan.h plan.cpp
    completeness.h completeness.cpp
    copier.h copier.cpp
    planexecutor.h planexecutor.cpp
    qfileinfogatherertask.h qfileinfogatherertask.cpp
    fileinfogatherer.h fileinfogatherer.cpp
    analysis.h analysis.cpp
    planmodel.h planmodel.cpp
    spitfiresettings.h spitfiresettings.cpp
    winpath.h winpath.cpp
    fsutil.h fsutil.cpp
)

# Windows only (UNVERIFIED: written without a Windows machine): file identity / real paths and the replace-existing
# rename. Each is declared in a header that every platform sees and implemented here.
if(WIN32)
    target_sources(corelib PRIVATE pathsafety_win.cpp fsutil_win.cpp)
endif()

find_package(Qt6 REQUIRED COMPONENTS Sql)
target_link_libraries(corelib PRIVATE Qt6::Core Qt6::Sql)
target_include_directories(corelib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_resources(corelib "profiles" PREFIX "/profiles" BASE "${CMAKE_SOURCE_DIR}/profiles" FILES
    ${CMAKE_SOURCE_DIR}/profiles/BBCSO.smtprofile/Contents/Info.plist
    ${CMAKE_SOURCE_DIR}/profiles/BBCSO.smtprofile/Contents/Resources/groups.json
    ${CMAKE_SOURCE_DIR}/profiles/BBCSO.smtprofile/Contents/Resources/images.json
    ${CMAKE_SOURCE_DIR}/profiles/BBCSO.smtprofile/Contents/Resources/library.json
    ${CMAKE_SOURCE_DIR}/profiles/BBCSO.smtprofile/Contents/Resources/mics.json
    ${CMAKE_SOURCE_DIR}/profiles/BBCSO.smtprofile/Contents/Resources/notes.md
    ${CMAKE_SOURCE_DIR}/profiles/BBCSO.smtprofile/Contents/Resources/spitfire.json
    ${CMAKE_SOURCE_DIR}/profiles/EricWhitacre.smtprofile/Contents/Info.plist
    ${CMAKE_SOURCE_DIR}/profiles/EricWhitacre.smtprofile/Contents/Resources/groups.json
    ${CMAKE_SOURCE_DIR}/profiles/EricWhitacre.smtprofile/Contents/Resources/images.json
    ${CMAKE_SOURCE_DIR}/profiles/EricWhitacre.smtprofile/Contents/Resources/library.json
    ${CMAKE_SOURCE_DIR}/profiles/EricWhitacre.smtprofile/Contents/Resources/mics.json
    ${CMAKE_SOURCE_DIR}/profiles/EricWhitacre.smtprofile/Contents/Resources/notes.md
    ${CMAKE_SOURCE_DIR}/profiles/EricWhitacre.smtprofile/Contents/Resources/spitfire.json
)

# Precompiled headers: the shared QtCore set (see the top-level CMakeLists.txt).
target_precompile_headers(corelib REUSE_FROM smt_pch_core)
