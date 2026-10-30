set(TEST_NAME utpl_common_lib)

add_executable(${TEST_NAME}
    src/base64.cpp
    src/time.cpp
    src/m3u8.cpp
    src/norm_rect.cpp
    src/exception.cpp
)

target_link_libraries(${TEST_NAME}
    PRIVATE
        common
        GTest::GTest 
        GTest::Main
)

add_test(
    NAME ${TEST_NAME}
    COMMAND ${TEST_NAME}
)