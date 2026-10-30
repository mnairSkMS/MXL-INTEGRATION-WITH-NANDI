find_package(Boost REQUIRED COMPONENTS program_options)

add_executable(keyEmbedder
    src/main.cpp
)

target_link_libraries(keyEmbedder
    PRIVATE
        common
        crypto
        toolsLicense

        Boost::program_options
)