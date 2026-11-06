add_executable(tst_winpath tst_winpath.cpp)
target_link_libraries(tst_winpath PRIVATE corelib Qt6::Test)
add_test(NAME tst_winpath COMMAND tst_winpath)

add_executable(tst_smt tst_smt.cpp)
target_link_libraries(tst_smt PRIVATE corelib Qt6::Test)
add_test(NAME tst_smt COMMAND tst_smt)

find_package(Qt6 REQUIRED COMPONENTS Sql)
add_executable(tst_librarydb tst_librarydb.cpp testlib.h)
target_link_libraries(tst_librarydb PRIVATE corelib Qt6::Test Qt6::Sql)
add_test(NAME tst_librarydb COMMAND tst_librarydb)

add_executable(tst_projectconfig tst_projectconfig.cpp)
target_link_libraries(tst_projectconfig PRIVATE corelib Qt6::Test)
add_test(NAME tst_projectconfig COMMAND tst_projectconfig)

add_executable(tst_pathsafety tst_pathsafety.cpp)
target_link_libraries(tst_pathsafety PRIVATE corelib Qt6::Test)
add_test(NAME tst_pathsafety COMMAND tst_pathsafety)

add_executable(tst_plan tst_plan.cpp)
target_link_libraries(tst_plan PRIVATE corelib Qt6::Test)
add_test(NAME tst_plan COMMAND tst_plan)

add_executable(tst_completeness tst_completeness.cpp)
target_link_libraries(tst_completeness PRIVATE corelib Qt6::Test)
add_test(NAME tst_completeness COMMAND tst_completeness)

add_executable(tst_copier tst_copier.cpp)
target_link_libraries(tst_copier PRIVATE corelib Qt6::Test)
add_test(NAME tst_copier COMMAND tst_copier)

add_executable(tst_planexecutor tst_planexecutor.cpp fakelibrary.h testlib.h)
target_link_libraries(tst_planexecutor PRIVATE corelib Qt6::Test Qt6::Sql)
add_test(NAME tst_planexecutor COMMAND tst_planexecutor)

# manual benchmark, not a ctest test: copybench <srcDir> <dstDir> [bufferMB] [concurrency] [both|copier|qfile]
add_executable(copybench copybench.cpp)
target_link_libraries(copybench PRIVATE corelib Qt6::Core)

add_executable(tst_gatherer tst_gatherer.cpp)
target_link_libraries(tst_gatherer PRIVATE corelib Qt6::Test)
add_test(NAME tst_gatherer COMMAND tst_gatherer)

add_executable(tst_core tst_core.cpp fakelibrary.h testlib.h)
target_link_libraries(tst_core PRIVATE corelib Qt6::Test Qt6::Sql)
add_test(NAME tst_core COMMAND tst_core)

add_executable(tst_analysis tst_analysis.cpp testlib.h)
target_link_libraries(tst_analysis PRIVATE corelib Qt6::Test Qt6::Sql)
add_test(NAME tst_analysis COMMAND tst_analysis)

add_executable(tst_progressformat tst_progressformat.cpp)
target_link_libraries(tst_progressformat PRIVATE corelib Qt6::Test)
add_test(NAME tst_progressformat COMMAND tst_progressformat)

add_executable(tst_cli tst_cli.cpp fakelibrary.h testlib.h)
target_link_libraries(tst_cli PRIVATE corelib Qt6::Test Qt6::Sql)
target_compile_definitions(tst_cli PRIVATE SMT_BIN="$<TARGET_FILE:cli>" SMT_PROFILES_DIR="${CMAKE_SOURCE_DIR}/profiles")
add_dependencies(tst_cli cli)
add_test(NAME tst_cli COMMAND tst_cli)

add_executable(tst_planmodel tst_planmodel.cpp)
target_link_libraries(tst_planmodel PRIVATE corelib Qt6::Test)
add_test(NAME tst_planmodel COMMAND tst_planmodel)

# The dialog is compiled in directly; MainWindow stays out.
add_executable(tst_plandialog tst_plandialog.cpp ../src/app/plandialog.cpp ../src/app/plandialog.h fakelibrary.h testlib.h)
target_include_directories(tst_plandialog PRIVATE ../src/app)
target_link_libraries(tst_plandialog PRIVATE corelib Qt6::Widgets Qt6::Test Qt6::Sql)
add_test(NAME tst_plandialog COMMAND tst_plandialog)
set_tests_properties(tst_plandialog PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

add_executable(tst_configerrornotifier tst_configerrornotifier.cpp ../src/app/configerrornotifier.h)
target_include_directories(tst_configerrornotifier PRIVATE ../src/app)
target_link_libraries(tst_configerrornotifier PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_configerrornotifier COMMAND tst_configerrornotifier)

add_executable(tst_spitfiresettings tst_spitfiresettings.cpp)
target_link_libraries(tst_spitfiresettings PRIVATE corelib Qt6::Test)
add_test(NAME tst_spitfiresettings COMMAND tst_spitfiresettings)

add_executable(tst_profile tst_profile.cpp)
target_link_libraries(tst_profile PRIVATE corelib Qt6::Test)
add_test(NAME tst_profile COMMAND tst_profile)

add_executable(tst_profileregistry tst_profileregistry.cpp)
target_link_libraries(tst_profileregistry PRIVATE corelib Qt6::Test Qt6::Gui)
add_test(NAME tst_profileregistry COMMAND tst_profileregistry)

add_executable(tst_profileartwork tst_profileartwork.cpp ../src/app/profileartwork.cpp ../src/app/profileartwork.h)
target_include_directories(tst_profileartwork PRIVATE ../src/app)
target_link_libraries(tst_profileartwork PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME tst_profileartwork COMMAND tst_profileartwork)
set_tests_properties(tst_profileartwork PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

add_executable(tst_profilepaths tst_profilepaths.cpp ../src/app/profilepaths.cpp ../src/app/profilepaths.h)
target_include_directories(tst_profilepaths PRIVATE ../src/app)
target_link_libraries(tst_profilepaths PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_profilepaths COMMAND tst_profilepaths)

add_executable(tst_micmatrixmodel tst_micmatrixmodel.cpp ../src/app/micmatrixmodel.cpp ../src/app/micmatrixmodel.h ../src/app/micmatrixview.cpp ../src/app/micmatrixview.h fakelibrary.h testlib.h)
target_include_directories(tst_micmatrixmodel PRIVATE ../src/app)
target_link_libraries(tst_micmatrixmodel PRIVATE corelib Qt6::Widgets Qt6::Test Qt6::Sql)
add_test(NAME tst_micmatrixmodel COMMAND tst_micmatrixmodel)
set_tests_properties(tst_micmatrixmodel PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

add_executable(tst_micmatrixview tst_micmatrixview.cpp ../src/app/darktheme.cpp ../src/app/palette.h ../src/app/micmatrixmodel.cpp ../src/app/micmatrixmodel.h ../src/app/micmatrixview.cpp ../src/app/micmatrixview.h fakelibrary.h testlib.h)
target_include_directories(tst_micmatrixview PRIVATE ../src/app)
target_link_libraries(tst_micmatrixview PRIVATE corelib Qt6::Widgets Qt6::Test Qt6::Sql)
add_test(NAME tst_micmatrixview COMMAND tst_micmatrixview)
set_tests_properties(tst_micmatrixview PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

add_executable(tst_imagefetcher tst_imagefetcher.cpp ../src/app/imagefetcher.cpp ../src/app/imagefetcher.h)
target_include_directories(tst_imagefetcher PRIVATE ../src/app)
target_link_libraries(tst_imagefetcher PRIVATE Qt6::Network Qt6::Gui Qt6::Test)
add_test(NAME tst_imagefetcher COMMAND tst_imagefetcher)

add_executable(tst_mainwindow tst_mainwindow.cpp ../src/app/mainwindow.cpp ../src/app/mainwindow.h ../src/app/mainwindow.ui
    ../src/app/micmatrixmodel.cpp ../src/app/micmatrixview.cpp ../src/app/elidedlabel.cpp ../src/app/plandialog.cpp
    ../src/app/profileartwork.cpp ../src/app/profilepaths.cpp ../src/app/imagefetcher.cpp ../src/app/darktheme.cpp
    fakelibrary.h testlib.h)
set_target_properties(tst_mainwindow PROPERTIES AUTORCC ON)
target_include_directories(tst_mainwindow PRIVATE ../src/app)
target_link_libraries(tst_mainwindow PRIVATE corelib Qt6::Widgets Qt6::Network Qt6::Test Qt6::Sql)
add_test(NAME tst_mainwindow COMMAND tst_mainwindow)
set_tests_properties(tst_mainwindow PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

get_property(all_tests DIRECTORY PROPERTY TESTS)
# The shared QtCore precompiled header (see the top-level CMakeLists.txt).
foreach(t ${all_tests})
    target_precompile_headers(${t} REUSE_FROM smt_pch_core)
endforeach()

# Every test runs with its own app data dir, so a plain `ctest` never touches the user's cache or settings.
foreach(t ${all_tests})
    set_property(TEST ${t} APPEND PROPERTY ENVIRONMENT "SMT_APP_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/appdata-${t}")
endforeach()
