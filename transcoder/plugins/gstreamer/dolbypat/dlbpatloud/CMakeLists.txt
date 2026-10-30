find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTLIBS REQUIRED
    gstreamer-audio-1.0
)

set(SOURCE_FILES 
    src/plugin.c
    src/dlbpatloud.c
    src/types.c
)

set(CMAKE_INSTALL_RPATH "$ORIGIN")

add_library(gstdlbpatloud
    SHARED
        ${SOURCE_FILES}
)

target_link_directories(gstdlbpatloud
    PRIVATE
        ../ext/dlb_paloud/lib/linux/x86_64
)

target_link_libraries(gstdlbpatloud
    PRIVATE
        ${GSTLIBS_LIBRARIES}

        gstdlbpatcommon

        paloud_linux_x86_64
        stdc++
)

target_include_directories(gstdlbpatloud
    PRIVATE
        ${GSTLIBS_INCLUDE_DIRS}
        
        include
        "$<TARGET_PROPERTY:gstdlbpatcommon,INTERFACE_INCLUDE_DIRECTORIES>"
        "$<TARGET_PROPERTY:utplcommon,INTERFACE_INCLUDE_DIRECTORIES>"
)

install(TARGETS gstdlbpatloud
    LIBRARY DESTINATION ${GST_PLUGINS_DIR}
)
