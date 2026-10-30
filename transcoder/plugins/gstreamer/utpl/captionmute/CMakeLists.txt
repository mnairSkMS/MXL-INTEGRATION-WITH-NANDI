find_package(PkgConfig REQUIRED)

#pkg_check_modules(GSTLIBS REQUIRED
#    gstreamer-audio-1.0
#)

set(SOURCE_FILES 
    src/plugin.c
    src/captionmute.c
)

set(CMAKE_INSTALL_RPATH "$ORIGIN")

add_library(utplcaptionmute
    SHARED
        ${SOURCE_FILES}
)

target_link_libraries(utplcaptionmute
    PRIVATE
        ${GSTLIBS_LIBRARIES}

        stdc++
)

target_include_directories(utplcaptionmute
    PRIVATE
        ${GSTLIBS_INCLUDE_DIRS}
        
        include
        "$<TARGET_PROPERTY:utplcommon,INTERFACE_INCLUDE_DIRECTORIES>"
        "$<TARGET_PROPERTY:gstdlbpatcommon,INTERFACE_INCLUDE_DIRECTORIES>"
)

install(TARGETS utplcaptionmute
    LIBRARY DESTINATION ${GST_PLUGINS_DIR}
)
