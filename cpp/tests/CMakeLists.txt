find_package(GTest QUIET)
if(NOT GTest_FOUND)
  message(STATUS "GTest not found on system, fetching from GitHub")
  include(FetchContent)
  FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.18.0
        GIT_SHALLOW TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(registry_test
    registry_test.cpp
)

target_link_libraries(registry_test
    PRIVATE
        nats_proto_schema_registry::schemaregistry
        GTest::gtest_main
)

set_target_properties(registry_test PROPERTIES FOLDER "schemaregistry/tests")

add_test(NAME registry_test COMMAND registry_test)

# Aggregate target that builds all tests.
add_custom_target(schemaregistry_tests)
add_dependencies(schemaregistry_tests registry_test)
set_target_properties(
    schemaregistry_tests
    PROPERTIES FOLDER "schemaregistry/tests"
)

