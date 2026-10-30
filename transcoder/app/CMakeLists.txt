find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options)
find_package(SQLiteCpp REQUIRED)
find_package(cpr REQUIRED)

find_package(gRPC CONFIG REQUIRED)

set(_PROTOBUF_PROTOC      protobuf::protoc)
set(_REFLECTION           gRPC::grpc++_reflection)
set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_GRPC_GRPCPP          gRPC::grpc++)

find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

set(proto_path      ${CMAKE_CURRENT_LIST_DIR}/grpc)
set(proto_out_path  ${CMAKE_CURRENT_BINARY_DIR}/proto)

set(proto_services transcoder)

foreach(proto_service ${proto_services})
    set(${proto_service}_proto_file ${proto_path}/proto/${proto_service}.proto)

    # Generated sources
    set(${proto_service}_proto_srcs "${proto_out_path}/${proto_service}.pb.cc")
    set(${proto_service}_proto_hdrs "${proto_out_path}/${proto_service}.pb.h")
    set(${proto_service}_grpc_srcs  "${proto_out_path}/${proto_service}.grpc.pb.cc")
    set(${proto_service}_grpc_hdrs  "${proto_out_path}/${proto_service}.grpc.pb.h")

    set(${proto_service}_sources
        "${${proto_service}_proto_srcs}"
        "${${proto_service}_proto_hdrs}"
        "${${proto_service}_grpc_srcs}"
        "${${proto_service}_grpc_hdrs}"
    )

    add_custom_command(
        OUTPUT
            ${${proto_service}_sources}

        COMMAND
            ${_PROTOBUF_PROTOC}

        ARGS
            --grpc_out ${CMAKE_CURRENT_BINARY_DIR}
            --cpp_out  ${CMAKE_CURRENT_BINARY_DIR}
            -I ${proto_path}
            --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE}
            ${${proto_service}_proto_file}

        DEPENDS
            ${${proto_service}_proto_file}
    )

    set(proto_sources
            ${proto_sources}
            ${${proto_service}_sources}
    )
endForeach()

add_library(transcoderGrpcApi
    ${proto_sources}
)

target_include_directories (transcoderGrpcApi
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR} # protobuf generated headers are in binary dir
)

target_link_libraries(transcoderGrpcApi
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF}
    ${_REFLECTION}
)

set(SOURCE_FILES 
    src/main.cpp
    src/application.cpp
    
    src/application/transcoder.cpp
    src/application/transcoder/api/grpc.cpp

    src/application/license/manager.cpp
    src/application/config/parser/json.cpp
    src/application/config/parser/json/parser.cpp

    src/application/event/provider/dummy.cpp
    
    src/application/event/provider/sqlite.cpp
    src/application/event/provider/sqlite/dao.cpp
)

add_executable(transcoder
    ${SOURCE_FILES}
)

target_include_directories(transcoder
    PRIVATE
        include
        ${CMAKE_CURRENT_BINARY_DIR}
        "$<TARGET_PROPERTY:utpl,INTERFACE_INCLUDE_DIRECTORIES>"
)

target_link_libraries(transcoder
    PRIVATE
        utpl
        common
        transcoderLicense
        transcoderGrpcApi
        spdlog::spdlog
        Boost::program_options
        SQLiteCpp
        cpr::cpr
)

if (CMAKE_BUILD_TYPE MATCHES "^[Dd][Ee][Bb][Uu][Gg]$")
    SET(LICENSE_BASE_DIR ${PROJECT_SOURCE_DIR}/license/dev/keys)
else()
    SET(LICENSE_BASE_DIR ${PROJECT_SOURCE_DIR}/license/prod/keys)
endif()

add_custom_command(
    TARGET
        transcoder POST_BUILD
    COMMENT
        "Embedding the license keys into the transcoder application using keys from ${LICENSE_BASE_DIR}"
    COMMAND
        $<TARGET_FILE:keyEmbedder>
    ARGS
        -p ${LICENSE_BASE_DIR}/license_ed25519_public_key.der
        -l ${LICENSE_BASE_DIR}/license_encryption_key.bin
        -i $<TARGET_FILE:transcoder>
    DEPENDS
        transcoder
        keyEmbedder
)

install(TARGETS transcoder
        RUNTIME DESTINATION ${UTPL_INSTALL_PREFIX}/bin
)
