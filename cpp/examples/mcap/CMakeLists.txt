cmake_minimum_required(VERSION 3.25)
project(mcap LANGUAGES CXX)

# Consume the schema registry library from the source tree.
# This relative path works because the example lives inside the registry repo.
# In your own project, point this to the nats_proto_schema_registry/cpp source
# tree or use FetchContent (see cpp/README.md).
if(NOT TARGET nats_proto_schema_registry::schemaregistry)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. schemaregistry)
endif()

set(SENSOR_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/sensor.proto)
set(SENSOR_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${SENSOR_GENERATED_DIR})

set(SENSOR_PB_SRC ${SENSOR_GENERATED_DIR}/sensor.pb.cc)
set(SENSOR_PB_HDR ${SENSOR_GENERATED_DIR}/sensor.pb.h)

# Resolve the protoc executable. Prefer the target exported by protobuf, then
# the FindProtobuf variable, then a plain program search.
if(TARGET protobuf::protoc)
    set(SENSOR_PROTOC_EXE $<TARGET_FILE:protobuf::protoc>)
elseif(DEFINED Protobuf_PROTOC_EXECUTABLE)
    set(SENSOR_PROTOC_EXE ${Protobuf_PROTOC_EXECUTABLE})
else()
    find_program(SENSOR_PROTOC_EXE NAMES protoc REQUIRED)
endif()

add_custom_command(
    OUTPUT ${SENSOR_PB_SRC} ${SENSOR_PB_HDR}
    COMMAND ${SENSOR_PROTOC_EXE}
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
        --cpp_out=${SENSOR_GENERATED_DIR}
        ${SENSOR_PROTO}
    DEPENDS ${SENSOR_PROTO} ${SENSOR_PROTOC_EXE}
    COMMENT "Generating C++ protobuf code for sensor.proto"
)

add_library(sensor_proto STATIC ${SENSOR_PB_SRC})
target_include_directories(sensor_proto PUBLIC ${SENSOR_GENERATED_DIR})
# Pull in the same protobuf/nats/glaze dependencies the registry uses.
target_link_libraries(sensor_proto
    PUBLIC
        nats_proto_schema_registry::schemaregistry
)

add_executable(producer producer.cpp)
target_link_libraries(producer
    PRIVATE
    nats_proto_schema_registry::schemaregistry sensor_proto
)

find_package(mcap QUIET)
if(NOT mcap_FOUND)
    message(STATUS "mcap not found on system, fetching from GitHub")
    include(FetchContent)
    FetchContent_Declare(
        mcap
        GIT_REPOSITORY https://github.com/foxglove/mcap.git
        GIT_TAG releases/cpp/v2.2.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(mcap)
endif()

add_executable(recorder recorder.cpp)
target_compile_definitions(recorder PRIVATE MCAP_IMPLEMENTATION)
target_include_directories(recorder PRIVATE ${mcap_SOURCE_DIR}/cpp/mcap/include)
target_link_libraries(recorder
    PRIVATE
    nats_proto_schema_registry::schemaregistry sensor_proto
)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LZ4 IMPORTED_TARGET liblz4)
    pkg_check_modules(ZSTD IMPORTED_TARGET libzstd)
    if(LZ4_FOUND AND ZSTD_FOUND)
        target_link_libraries(recorder PRIVATE PkgConfig::LZ4 PkgConfig::ZSTD)
    endif()
endif()

if(NOT TARGET PkgConfig::LZ4 OR NOT TARGET PkgConfig::ZSTD)
    find_library(LZ4_LIBRARY NAMES lz4)
    find_library(ZSTD_LIBRARY NAMES zstd)
    if(LZ4_LIBRARY AND ZSTD_LIBRARY)
        target_link_libraries(recorder PRIVATE ${LZ4_LIBRARY} ${ZSTD_LIBRARY})
    endif()
endif()
