find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTLIBS REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-pbutils-1.0
    gstreamer-controller-1.0
    gstreamer-mpegts-1.0
)

pkg_check_modules(LIBAV REQUIRED
    libavdevice
    libavfilter
    libavformat
    libavcodec
    libswresample
    libswscale
    libavutil

    libpcsclite
)

find_package(spdlog REQUIRED)
find_package(Boost REQUIRED COMPONENTS url)
find_package(pugixml REQUIRED)
find_package(cpr REQUIRED)


add_executable(esamClient main.cpp)

# Link libraries
# target_link_libraries(esam_test_app PRIVATE
#     pugixml
#     boost_system
#     boost_filesystem
#     gstmpegts-1.0
#     glib-2.0
# )

# add_executable(esamClient
#     src/utpl/esam/client.cpp
#     src/utpl/esam/model.cpp
#     src/utpl/esam/test.cpp
# )

target_include_directories(esamClient
    PUBLIC
        include
        ${GSTLIBS_INCLUDE_DIRS}
        ${LIBAV_INCLUDE_DIRS}
)

target_link_libraries(esamClient
    PUBLIC
        ${LIBAV_LIBRARIES}
        ${GSTLIBS_LIBRARIES}
        spdlog::spdlog
        Boost::url
        pugixml::pugixml
        utpl
    PRIVATE cpr::cpr
)


# Add any additional libraries or dependencies here
# target_link_libraries(esam_test_app PRIVATE <additional_libraries>)