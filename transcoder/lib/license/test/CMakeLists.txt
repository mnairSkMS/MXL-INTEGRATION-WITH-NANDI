set(TEST_NAME utpl_license_lib)

add_executable(${TEST_NAME}
    src/serialize.cpp
)

target_link_libraries(${TEST_NAME}
    PRIVATE
        toolsLicense
        GTest::GTest 
        GTest::Main
)

add_test(
    NAME ${TEST_NAME}
    COMMAND ${TEST_NAME}
)