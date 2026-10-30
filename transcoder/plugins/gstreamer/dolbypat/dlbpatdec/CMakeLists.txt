find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTLIBS REQUIRED
    gstreamer-audio-1.0
)

set(SOURCE_FILES 
    src/plugin.c
    src/dlbpatdec.c
)

set(CMAKE_INSTALL_RPATH "$ORIGIN")

add_library(gstdlbpatdec
    SHARED
        ${SOURCE_FILES}
)

target_link_directories(gstdlbpatdec
    PRIVATE
        ../ext/dlb_padc/lib/linux/x86_64
)

target_link_libraries(gstdlbpatdec
    PRIVATE
        ${GSTLIBS_LIBRARIES}

        gstdlbpatcommon

        padc_linux_x86_64
        stdc++
)

target_include_directories(gstdlbpatdec
    PRIVATE
        ${GSTLIBS_INCLUDE_DIRS}
        
        include
        "$<TARGET_PROPERTY:gstdlbpatcommon,INTERFACE_INCLUDE_DIRECTORIES>"
        "$<TARGET_PROPERTY:utplcommon,INTERFACE_INCLUDE_DIRECTORIES>"
)

install(TARGETS gstdlbpatdec
    LIBRARY DESTINATION ${GST_PLUGINS_DIR}
)
