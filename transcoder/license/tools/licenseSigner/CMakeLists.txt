find_package(Boost REQUIRED COMPONENTS program_options)

add_executable(licenseSigner
    src/main.cpp
)

target_link_libraries(licenseSigner 
    PUBLIC
        common

    PRIVATE
        toolsLicense
        Boost::program_options
)

install(TARGETS licenseSigner
    RUNTIME DESTINATION ${UTPL_INSTALL_PREFIX}/bin
    COMPONENT license_tools
    EXCLUDE_FROM_ALL
)