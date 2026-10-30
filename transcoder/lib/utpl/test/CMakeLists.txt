add_executable(utpl_playlist_parser_test
    src/parser.cpp
    ${CMAKE_SOURCE_DIR}/lib/utpl/include
)


target_include_directories(utpl_playlist_parser_test
    PRIVATE
        ${GSTLIBS_INCLUDE_DIRS}
        ${LIBAV_INCLUDE_DIRS}
        ${OTHERDEPS_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/lib/utpl/include
        ${CMAKE_SOURCE_DIR}/lib/common/include
)

target_link_libraries(utpl_playlist_parser_test
    PRIVATE
    utpl
    GTest::GTest
    GTest::Main
)

add_test(
    NAME utpl_playlist_parser_test
    COMMAND utpl_playlist_parser_test
)

add_executable(utpl_scte_section_test
    src/scte.cpp
)

target_include_directories(utpl_scte_section_test
    PRIVATE
        ${GSTLIBS_INCLUDE_DIRS}
        ${LIBAV_INCLUDE_DIRS}
        ${OTHERDEPS_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/lib/utpl/include
        ${CMAKE_SOURCE_DIR}/lib/common/include
        # scheduler/scte.h is internal to the library, not part of its public headers.
        ${CMAKE_SOURCE_DIR}/lib/utpl/src
)

target_link_libraries(utpl_scte_section_test
    PRIVATE
    utpl
    GTest::GTest
)

add_test(
    NAME utpl_scte_section_test
    COMMAND utpl_scte_section_test
)

add_executable(utpl_audio_blank_state_test
    src/audioBlankState.cpp
)

target_include_directories(utpl_audio_blank_state_test
    PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/utpl/src
)

target_link_libraries(utpl_audio_blank_state_test
    PRIVATE
        GTest::GTest
        GTest::Main
)

add_test(
    NAME utpl_audio_blank_state_test
    COMMAND utpl_audio_blank_state_test
)

add_executable(utpl_audio_blank_hold_test
    src/audioBlankHold.cpp
)

target_include_directories(utpl_audio_blank_hold_test
    PRIVATE
        ${GSTLIBS_INCLUDE_DIRS}
        ${LIBAV_INCLUDE_DIRS}
        ${OTHERDEPS_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/lib/utpl/include
        ${CMAKE_SOURCE_DIR}/lib/common/include
)

target_link_libraries(utpl_audio_blank_hold_test
    PRIVATE
    utpl
    GTest::GTest
    GTest::Main
)

add_test(
    NAME utpl_audio_blank_hold_test
    COMMAND utpl_audio_blank_hold_test
)

add_executable(utpl_dash_emsg_test
    src/dashEmsg.cpp
)

target_include_directories(utpl_dash_emsg_test
    PRIVATE
        ${GSTLIBS_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/lib/utpl/include
        ${CMAKE_SOURCE_DIR}/lib/common/include
        # input/dash/emsg.h is internal to the library, not part of its public headers.
        ${CMAKE_SOURCE_DIR}/lib/utpl/src
)

target_link_libraries(utpl_dash_emsg_test
    PRIVATE
    utpl
    GTest::GTest
    GTest::Main
)

add_test(
    NAME utpl_dash_emsg_test
    COMMAND utpl_dash_emsg_test
)

add_executable(utpl_dash_presentation_test
    src/dashPresentation.cpp
)

target_include_directories(utpl_dash_presentation_test
    PRIVATE
        ${GSTLIBS_INCLUDE_DIRS}
        ${LIBAV_INCLUDE_DIRS}
        ${OTHERDEPS_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/lib/utpl/include
        ${CMAKE_SOURCE_DIR}/lib/common/include
        # output/dash/presentation.h and scheduler/scte.h are internal to the library.
        ${CMAKE_SOURCE_DIR}/lib/utpl/src
)

target_link_libraries(utpl_dash_presentation_test
    PRIVATE
    utpl
    GTest::GTest
)

add_test(
    NAME utpl_dash_presentation_test
    COMMAND utpl_dash_presentation_test
)

add_executable(utpl_stream_parameters_test
    src/streamParameters.cpp
)

target_include_directories(utpl_stream_parameters_test
    PRIVATE
        ${GSTLIBS_INCLUDE_DIRS}
        ${LIBAV_INCLUDE_DIRS}
        ${OTHERDEPS_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/lib/utpl/include
        ${CMAKE_SOURCE_DIR}/lib/common/include
)

target_link_libraries(utpl_stream_parameters_test
    PRIVATE
    utpl
    GTest::GTest
    GTest::Main
)

add_test(
    NAME utpl_stream_parameters_test
    COMMAND utpl_stream_parameters_test
)
