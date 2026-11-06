qt_add_executable(cli main.cpp)

target_link_libraries(cli PRIVATE corelib Qt6::Core)

set_target_properties(cli PROPERTIES OUTPUT_NAME mictrim)

if(WIN32) # UNVERIFIED: next to the GUI executable, so the Qt DLLs deployed for it serve both
    include(GNUInstallDirs)
    install(TARGETS cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
