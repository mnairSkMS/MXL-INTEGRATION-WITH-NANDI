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
)

pkg_check_modules(OTHERDEPS REQUIRED
    tsduck
    libpcsclite
)

find_package(spdlog REQUIRED)
find_package(Boost REQUIRED COMPONENTS url)
find_package(pugixml REQUIRED)
find_package(cpr REQUIRED)

set(SOURCE_FILES 
    src/utpl.cpp

    src/utpl/common/utils/gstreamer.cpp
    
    src/utpl/pipeline/pad.cpp
    src/utpl/pipeline/element.cpp
    src/utpl/pipeline/bin.cpp
    src/utpl/pipeline/pipe.cpp
    src/utpl/pipeline/caps.cpp
    src/utpl/pipeline/utils.cpp

    src/utpl/transcoder/engine.cpp
    src/utpl/transcoder/output/utils.cpp
    src/utpl/transcoder/output/muxer/mpegts.cpp

    src/utpl/transcoder/output/sink.cpp
    src/utpl/transcoder/output/sink/hls.cpp
    src/utpl/transcoder/output/sink/file.cpp
    src/utpl/transcoder/output/sink/udp.cpp
    src/utpl/transcoder/output/sink/srt.cpp
    src/utpl/transcoder/output/sink/s3.cpp
    src/utpl/transcoder/output/sink/dash.cpp
    src/utpl/transcoder/output/dash/presentation.cpp

    src/utpl/transcoder/output/single.cpp
    src/utpl/transcoder/output/mxl.cpp
    src/utpl/transcoder/output/group.cpp

    src/utpl/transcoder/input/single.cpp
    src/utpl/transcoder/input/dash/emsg.cpp
    src/utpl/transcoder/input/standard.cpp
    src/utpl/transcoder/input/playlist.cpp
    src/utpl/transcoder/input/overlay.cpp
    
    src/utpl/transcoder/event.cpp
    src/utpl/transcoder/event/scheduler.cpp
    src/utpl/transcoder/event/scheduler/scte.cpp
    src/utpl/transcoder/event/scheduler/overlay.cpp
    src/utpl/transcoder/event/scheduler/slate.cpp
    src/utpl/transcoder/event/scheduler/squeeze.cpp
    src/utpl/transcoder/event/scheduler/spliceInsert.cpp
    src/utpl/transcoder/event/scheduler/timeSignal.cpp
    src/utpl/transcoder/event/scheduler/returnToNetwork.cpp

    src/utpl/transcoder/stream.cpp
    src/utpl/transcoder/stream/pad.cpp
    src/utpl/transcoder/stream/input/image.cpp
    src/utpl/transcoder/stream/input/video.cpp
    src/utpl/transcoder/stream/input/audio.cpp
    src/utpl/transcoder/stream/input/base.cpp
    
    src/utpl/transcoder/channel.cpp
    src/utpl/transcoder/channel/processor.cpp
    src/utpl/transcoder/channel/processor/passthrough.cpp
    src/utpl/transcoder/channel/processor/transcoder/audio.cpp
    src/utpl/transcoder/channel/processor/transcoder/video.cpp
    src/utpl/transcoder/channel/processor/transcoder/flowline.cpp
    src/utpl/transcoder/channel/processor/transcoder/mxlflowline.cpp
    src/utpl/transcoder/channel/processor/transcoder/dolby.cpp
    src/utpl/transcoder/channel/compositor.cpp
    
    src/utpl/transcoder/channel/connector/passthrough.cpp    
    src/utpl/transcoder/channel/connector/complex/video.cpp
    src/utpl/transcoder/channel/connector/complex/audio.cpp
    src/utpl/transcoder/channel/connector/complex/dolby.cpp

    src/utpl/transcoder/scte/eventTracker.cpp
    src/utpl/transcoder/blanker.cpp
    src/utpl/transcoder/adBreak.cpp
    src/utpl/transcoder/preview.cpp
    src/utpl/transcoder/mxlframegrid.cpp
    src/utpl/transcoder/mxltap.cpp

    src/utpl/esam/client.cpp
    src/utpl/esam/model.cpp

    src/utpl/hls/streamer.cpp
    src/utpl/hls/streamer/uploader/base.cpp
    src/utpl/hls/streamer/uploader/file.cpp
    src/utpl/hls/streamer/uploader/http.cpp
    src/utpl/hls/playlist.cpp
    src/utpl/hls/segment/storage/factory/memory.cpp
    
    src/utpl/playlist/parser.cpp
)

add_library(utpl 
    SHARED
        ${SOURCE_FILES}
)

target_link_libraries(utpl
    PUBLIC
        common
        ${LIBAV_LIBRARIES}
        ${GSTLIBS_LIBRARIES}
        ${OTHERDEPS_LIBRARIES}
        spdlog::spdlog
        Boost::url
        pugixml::pugixml
    PRIVATE cpr::cpr
)

target_compile_definitions(utpl
    PRIVATE
        UTPL_VERSION="${UTPL_VERSION}"
)

target_include_directories(utpl
    PUBLIC
        include
        ${GSTLIBS_INCLUDE_DIRS}
        ${LIBAV_INCLUDE_DIRS}
        ${OTHERDEPS_INCLUDE_DIRS}
)

add_subdirectory(test)

install(TARGETS utpl
        LIBRARY DESTINATION ${UTPL_INSTALL_PREFIX}/lib
)