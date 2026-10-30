find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTLIBS REQUIRED
    gstreamer-audio-1.0
)

set(SOURCE_FILES 
    src/plugin.c
    src/dlbpatenc.c
    src/types.c
    src/utils.c
)

set(CMAKE_INSTALL_RPATH "$ORIGIN")

add_library(gstdlbpatenc
    SHARED
        ${SOURCE_FILES}
)

target_link_directories(gstdlbpatenc
    PRIVATE
        ../ext/dlb_paec/lib/linux/x86_64
)

target_link_libraries(gstdlbpatenc
    PRIVATE
        ${GSTLIBS_LIBRARIES}

        gstdlbpatcommon

        paec_linux_x86_64
        stdc++
)

target_include_directories(gstdlbpatenc
    PRIVATE
        ${GSTLIBS_INCLUDE_DIRS}
        
        include
        "$<TARGET_PROPERTY:gstdlbpatcommon,INTERFACE_INCLUDE_DIRECTORIES>"
        "$<TARGET_PROPERTY:utplcommon,INTERFACE_INCLUDE_DIRECTORIES>"
)

install(TARGETS gstdlbpatenc
    LIBRARY DESTINATION ${GST_PLUGINS_DIR}
)
