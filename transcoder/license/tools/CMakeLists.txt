add_subdirectory(keyEmbedder)
add_subdirectory(keyGenerator)
add_subdirectory(licenseGenerator)
add_subdirectory(licenseSigner)
add_subdirectory(licenseInfo)

add_custom_target(install-license-tools
    DEPENDS keyGenerator licenseGenerator 
    COMMAND "${CMAKE_COMMAND}" 
        -DCMAKE_INSTALL_COMPONENT=license_tools 
        -P "${CMAKE_BINARY_DIR}/cmake_install.cmake"
)
