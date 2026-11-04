

find_package(protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

add_library(proto_gen STATIC tradebot/v1/person.proto)

target_link_libraries(
    proto_gen
    PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
)

protobuf_generate(TARGET proto_gen LANGUAGE cpp IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
protobuf_generate(TARGET proto_gen LANGUAGE grpc
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    PLUGIN "protoc-gen-grpc=\$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
    IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

## library target -> .proto files are sources

target_include_directories(
    proto_gen 
    PUBLIC 
    ${CMAKE_CURRENT_BINARY_DIR})