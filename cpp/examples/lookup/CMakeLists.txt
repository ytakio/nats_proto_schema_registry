cmake_minimum_required(VERSION 3.25)
project(lookup LANGUAGES CXX)

# Consume the schema registry library from the source tree.
# This relative path works because the example lives inside the registry repo.
# In your own project, point this to the nats_proto_schema_registry/cpp source
# tree or use FetchContent (see cpp/README.md).
if(NOT TARGET nats_proto_schema_registry::schemaregistry)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. schemaregistry)
endif()

add_executable(lookup main.cpp)
target_link_libraries(lookup
    PRIVATE
        nats_proto_schema_registry::schemaregistry
)
