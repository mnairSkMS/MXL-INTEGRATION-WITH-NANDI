find_package(Boost REQUIRED COMPONENTS filesystem)
find_package(PkgConfig REQUIRED)

pkg_check_modules(ELFDEPS REQUIRED
    libelf
)

set(SRC_BASE src/utpl/)

set(LICENSE_BASE_SOURCES
    ${SRC_BASE}/license/utils/elf/reader.cpp
    ${SRC_BASE}/license/utils/elf/hasher.cpp
    ${SRC_BASE}/license/elf/keyLoader.cpp
    ${SRC_BASE}/license/signature.cpp
    ${SRC_BASE}/license/file/serializer.cpp
    ${SRC_BASE}/license/file/serializer/json.cpp
    ${SRC_BASE}/license/serializer/json.cpp
    ${SRC_BASE}/license.cpp
)

add_library(transcoderLicense STATIC
    ${LICENSE_BASE_SOURCES}
)

target_include_directories(transcoderLicense 
    PUBLIC
        include
)

target_link_libraries(transcoderLicense
    PUBLIC
        common
        crypto

    PRIVATE
        ${ELFDEPS_STATIC_LIBRARIES}
        Boost::filesystem
)

set_property(TARGET transcoderLicense PROPERTY POSITION_INDEPENDENT_CODE ON)

add_library(toolsLicense STATIC
    ${LICENSE_BASE_SOURCES}

    ${SRC_BASE}/license/utils/elf/modifier.cpp
    ${SRC_BASE}/license/elf/keyStorer.cpp
)

target_include_directories(toolsLicense 
    PUBLIC
        include
)

target_link_libraries(toolsLicense
    PUBLIC
        common
        crypto
    PRIVATE
        ${ELFDEPS_STATIC_LIBRARIES}
        Boost::filesystem
)

set_property(TARGET toolsLicense PROPERTY POSITION_INDEPENDENT_CODE ON)

add_subdirectory(test)
