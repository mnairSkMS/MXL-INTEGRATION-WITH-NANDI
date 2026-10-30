find_package(Boost REQUIRED COMPONENTS program_options)

add_executable(licenseInfo
    src/main.cpp
)

target_link_libraries(licenseInfo 
    PUBLIC
        common

    PRIVATE
        toolsLicense
        Boost::program_options
)

install(TARGETS licenseInfo
    RUNTIME DESTINATION ${UTPL_INSTALL_PREFIX}/bin
    COMPONENT license_tools
    EXCLUDE_FROM_ALL
)