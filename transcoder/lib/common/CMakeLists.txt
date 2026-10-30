find_package(PkgConfig REQUIRED)
find_package(fmt REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS url)

pkg_check_modules(GSTLIBS REQUIRED
    gstreamer-1.0
)

set(SRC_BASE src/utpl/common)

add_library(common SHARED
    ${SRC_BASE}/logger.cpp
    ${SRC_BASE}/debug.cpp
    ${SRC_BASE}/utils.cpp
    ${SRC_BASE}/utils/m3u8.cpp
    ${SRC_BASE}/time.cpp
    ${SRC_BASE}/types.cpp
)

target_link_libraries(common
    PUBLIC
        fmt::fmt
        ${GSTLIBS_LIBRARIES}
        OpenSSL::Crypto
        Boost::url
)

target_include_directories(common
    PUBLIC
        include
        ${GSTLIBS_INCLUDE_DIRS}
)

add_subdirectory(test)

install(TARGETS common
    LIBRARY DESTINATION ${UTPL_INSTALL_PREFIX}/lib
    COMPONENT license_tools
)