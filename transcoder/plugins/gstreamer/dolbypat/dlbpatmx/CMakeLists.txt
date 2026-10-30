find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTLIBS REQUIRED
    gstreamer-audio-1.0
)

set(SOURCE_FILES 
    src/plugin.c
    src/dlbpatmx.c
    src/types.c
)

set(CMAKE_INSTALL_RPATH "$ORIGIN")

add_library(gstdlbpatmx
    SHARED
        ${SOURCE_FILES}
)

target_link_directories(gstdlbpatmx
    PRIVATE
        ../ext/dlb_padmx/lib/linux/x86_64
)

target_link_libraries(gstdlbpatmx
    PRIVATE
        ${GSTLIBS_LIBRARIES}

        gstdlbpatcommon

        padmx_linux_x86_64
        stdc++
)

target_include_directories(gstdlbpatmx
    PRIVATE
        ${GSTLIBS_INCLUDE_DIRS}
        
        include
        "$<TARGET_PROPERTY:gstdlbpatcommon,INTERFACE_INCLUDE_DIRECTORIES>"
        "$<TARGET_PROPERTY:utplcommon,INTERFACE_INCLUDE_DIRECTORIES>"
)

install(TARGETS gstdlbpatmx
    LIBRARY DESTINATION ${GST_PLUGINS_DIR}
)
