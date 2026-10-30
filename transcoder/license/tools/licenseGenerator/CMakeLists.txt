find_package(Boost REQUIRED COMPONENTS program_options)

add_executable(licenseGenerator
    src/main.cpp
)

target_link_libraries(licenseGenerator 
    PUBLIC
        common

    PRIVATE
        toolsLicense
        Boost::program_options
)

install(TARGETS licenseGenerator
    RUNTIME DESTINATION ${UTPL_INSTALL_PREFIX}/bin
    COMPONENT license_tools
    EXCLUDE_FROM_ALL
)