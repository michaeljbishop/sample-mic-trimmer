qt_standard_project_setup()

set(CMAKE_AUTORCC ON)

qt_add_executable(SampleMicTrimmer
  main.cpp
  mainwindow.cpp
  mainwindow.h
  mainwindow.ui
  micmatrixmodel.h micmatrixmodel.cpp
  micmatrixview.h micmatrixview.cpp
  elidedlabel.h elidedlabel.cpp
  plandialog.h plandialog.cpp
  profileartwork.h profileartwork.cpp
  profilepaths.h profilepaths.cpp
  imagefetcher.h imagefetcher.cpp
  configerrornotifier.h
  darktheme.h darktheme.cpp palette.h
)


target_include_directories(SampleMicTrimmer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(SampleMicTrimmer PRIVATE corelib Qt6::Widgets Qt6::Network)

if(APPLE)
# Variables read by Info.plist.in. Bundle id = kOrganizationName + "." + kApplicationName without spaces (corelib.h).
set(MACOSX_BUNDLE_GUI_IDENTIFIER name.mbishop.SampleMicTrimmer)
set(MACOSX_BUNDLE_BUNDLE_NAME "Sample Mic Trimmer")
set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
set(MACOSX_BUNDLE_EXECUTABLE_NAME "Sample Mic Trimmer")

set_target_properties(SampleMicTrimmer PROPERTIES
    OUTPUT_NAME "Sample Mic Trimmer"
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
)

# App icon (generated from artwork once; the .icns is checked in).
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/icons/AppIcon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(SampleMicTrimmer PRIVATE icons/AppIcon.icns)

# Built-in profiles as real folders in Contents/Resources/Profiles. Source properties (not a post-build
# copy) so the files are part of the bundle that `install(TARGETS ... BUNDLE)` installs.
file(GLOB_RECURSE profile_files CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/profiles/*.smtprofile/*)
list(FILTER profile_files EXCLUDE REGEX "/\\.DS_Store$")
foreach(f ${profile_files})
    file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR}/profiles ${f})
    get_filename_component(reldir ${rel} DIRECTORY)
    set_source_files_properties(${f} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Profiles/${reldir})
    target_sources(SampleMicTrimmer PRIVATE ${f})
endforeach()
elseif(WIN32)
# Windows (UNVERIFIED: no Windows machine was available). A GUI executable without a console window, and the
# built-in profiles as a Profiles/ folder next to it (bundledProfilesDir() looks there), in the build tree and
# in the install.
set_target_properties(SampleMicTrimmer PROPERTIES
    OUTPUT_NAME "SampleMicTrimmer"
    WIN32_EXECUTABLE ON
)
# The .ico is embedded as the executable's icon through a resource script.
target_sources(SampleMicTrimmer PRIVATE icons/appicon.rc)
add_custom_command(TARGET SampleMicTrimmer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/profiles $<TARGET_FILE_DIR:SampleMicTrimmer>/Profiles
)
endif()

include(GNUInstallDirs)

install(TARGETS SampleMicTrimmer
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(WIN32)
    # The profiles beside the executables (the command line tool is installed next to the GUI by src/cli, so one
    # windeployqt run covers both).
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/profiles/ DESTINATION ${CMAKE_INSTALL_BINDIR}/Profiles
            PATTERN ".DS_Store" EXCLUDE)
endif()

# macdeployqt / windeployqt through Qt's deploy script. The app uses QSQLITE only (QtSql is a dependency of corelib),
# so on Windows the Qt DLLs, the platform plugin, the styles and sqldrivers/qsqlite.dll must be shipped.
qt_generate_deploy_app_script(
    TARGET SampleMicTrimmer
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
if(APPLE)
# macdeployqt also bundles the Mimer SQL driver, which needs an absent client library; the app only uses QSQLITE.
install(CODE "file(REMOVE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/Sample Mic Trimmer.app/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib\")")
endif()
