if (NOT DEFINED GST_PLUGINS_DIR)
    find_package(PkgConfig REQUIRED)

    pkg_get_variable(GST_PLUGINS_DIR gstreamer-1.0 pluginsdir)
endif ()

add_subdirectory(common)
add_subdirectory(dolbypat)
add_subdirectory(utpl)