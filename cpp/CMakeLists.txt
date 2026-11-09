cmake_minimum_required(VERSION 3.25)

project(nats_proto_schema_registry
    VERSION 0.1.0
    DESCRIPTION "C++20 header-only NATS protobuf schema registry"
    HOMEPAGE_URL "https://github.com/nats-io/nats_proto_schema_registry"
    LANGUAGES CXX
)

# Options default to ON when this project is the top-level build, and OFF when
# it is consumed as a subdirectory/FetchContent dependency. Downstream projects
# can override these before calling add_subdirectory/FetchContent_MakeAvailable.
option(SCHEMAREGISTRY_BUILD_TESTS
    "Build the schema registry tests"
    ${PROJECT_IS_TOP_LEVEL}
)
option(SCHEMAREGISTRY_BUILD_EXAMPLES
    "Build the schema registry examples"
    ${PROJECT_IS_TOP_LEVEL}
)

# Header-only schema registry library.
#
# This library is intended to be consumed via add_subdirectory() or
# FetchContent. It is not installed system-wide, so there are no install rules.
# Consumers link against the `nats_proto_schema_registry::schemaregistry`
# interface target.

# Resolve protobuf, nats.c and glaze. System packages are preferred; missing
# dependencies are fetched automatically with FetchContent.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SchemaRegistryDependencies.cmake)

add_library(schemaregistry INTERFACE)
add_library(nats_proto_schema_registry::schemaregistry ALIAS schemaregistry)

target_include_directories(schemaregistry
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(schemaregistry
    INTERFACE
        protobuf::libprotobuf
        cnats::nats
        glaze::glaze
)

target_compile_features(schemaregistry INTERFACE cxx_std_20)

if(SCHEMAREGISTRY_BUILD_TESTS)
  enable_testing()
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()

if(SCHEMAREGISTRY_BUILD_EXAMPLES)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/examples/register)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/examples/lookup)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/examples/mcap)
endif()

