find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTLIBS REQUIRED
    gstreamer-audio-1.0
    gstreamer-base-1.0
)

set(SOURCE_FILES 
    src/metadata.c
    src/timeline.c
    src/queue.c
    src/common.c
    src/utils.c
)

add_library(gstdlbpatcommon
    SHARED
        ${SOURCE_FILES}
)

target_link_directories(gstdlbpatcommon
    PRIVATE
        ../ext/dlb_pamif/lib/linux/x86_64
        ../ext/dlb_padc/lib/linux/x86_64
        ../ext/dlb_padmx/lib/linux/x86_64
        ../ext/dlb_paloud/lib/linux/x86_64
        ../ext/dlb_paec/lib/linux/x86_64
)

target_link_libraries(gstdlbpatcommon
    PRIVATE
        ${GSTLIBS_LIBRARIES}

        pamif_linux_x86_64
        padc_linux_x86_64
        padmx_linux_x86_64
        paloud_linux_x86_64
        paec_linux_x86_64
        stdc++
)

target_include_directories(gstdlbpatcommon
    PUBLIC
        include

        ../ext/common/include
        ../ext/dlb_pamif/include
        ../ext/dlb_paec/include
        ../ext/dlb_padc/include
        ../ext/dlb_padmx/include
        ../ext/dlb_paloud/include
        ../ext/dlb_pamif/include

    PRIVATE
        ${GSTLIBS_INCLUDE_DIRS}
)

install(TARGETS gstdlbpatcommon
    LIBRARY DESTINATION ${GST_PLUGINS_DIR}
)
