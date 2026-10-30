cmake_minimum_required(VERSION 3.14)

project(universal-transcoding-pipeline
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)

file(STRINGS "${PROJECT_SOURCE_DIR}/../version.txt" UTPL_VERSION)

if (NOT CMAKE_BUILD_TYPE)
    message( "Build Type not set, defaulting to Debug..." )
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_FLAGS_DEBUG   
    "${CMAKE_CXX_FLAGS_DEBUG} -O0 -ggdb -pipe -fno-omit-frame-pointer -fsanitize=address"
)

set(CMAKE_LINKER_FLAGS_DEBUG 
    "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address"
)

set(CMAKE_CXX_FLAGS_RELEASE 
    "${CMAKE_CXX_FLAGS_RELEASE} -O2 -pipe -s"
)

if (NOT DEFINED UTPL_INSTALL_PREFIX)
    set(UTPL_INSTALL_PREFIX /opt/utpl)
endif ()

# testing
find_package(GTest REQUIRED)
set(CMAKE_CTEST_ARGUMENTS "--verbose")
set(CMAKE_CTEST_ARGUMENTS "--output-on-failure")
enable_testing()

add_subdirectory(lib)
add_subdirectory(license)
add_subdirectory(app)
add_subdirectory(test)
add_subdirectory(plugins)
