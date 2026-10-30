find_package(OpenSSL REQUIRED)

set(SRC_BASE src/utpl/crypto)

add_library(crypto STATIC
    ${SRC_BASE}/manager.cpp
    ${SRC_BASE}/manager/openssl/manager.cpp
    ${SRC_BASE}/manager/openssl/hasher.cpp
    ${SRC_BASE}/manager/openssl/cipher.cpp
    ${SRC_BASE}/manager/openssl/signature.cpp
    ${SRC_BASE}/manager/openssl/types.cpp
)

set_property(TARGET crypto PROPERTY POSITION_INDEPENDENT_CODE ON)

target_link_libraries(crypto
    PUBLIC
        common
    PRIVATE
        OpenSSL::Crypto
)

target_include_directories(crypto
    PUBLIC
        include
)

add_subdirectory(test)