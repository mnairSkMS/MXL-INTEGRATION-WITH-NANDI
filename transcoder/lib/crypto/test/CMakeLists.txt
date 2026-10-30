set(TEST_NAME utpl_crypto_lib)

add_executable(${TEST_NAME}
    src/hasher.cpp
    src/cipher.cpp
    src/signature.cpp
)

target_link_libraries(${TEST_NAME}
    PRIVATE
        crypto
        GTest::GTest 
        GTest::Main
)

add_test(
    NAME ${TEST_NAME}
    COMMAND ${TEST_NAME}
)