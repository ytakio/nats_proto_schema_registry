# Dependency resolution helpers for the schema registry C++ project.
#
# Core library dependencies (protobuf, nats.c, glaze) are resolved here so that
# the header-only `schemaregistry` target can be consumed without forcing
# downstream projects to repeat FetchContent boilerplate. Optional dependencies
# that are only needed by specific examples or tests are resolved in the
# subdirectory that actually needs them.
#
# Version policy:
#   - System packages are preferred.
#   - find_package() is called without a minimum version so the build accepts
#     whatever compatible version is installed locally.
#   - FetchContent tags track the latest stable release of each dependency so
#     the fallback build always uses an up-to-date upstream version.

include(FetchContent)

# -------------------------------------------------------------------------
# Protobuf
# -------------------------------------------------------------------------
find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
  message(STATUS "Protobuf not found on system, fetching from GitHub")
  FetchContent_Declare(
        protobuf
        GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
        GIT_TAG v36.2
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR cmake
    )
  set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(protobuf)
endif()

# -------------------------------------------------------------------------
# nats.c
# -------------------------------------------------------------------------
find_package(cnats CONFIG QUIET)
if(NOT cnats_FOUND)
  message(STATUS "nats.c not found on system, fetching from GitHub")
  FetchContent_Declare(
        nats_c
        GIT_REPOSITORY https://github.com/nats-io/nats.c.git
        GIT_TAG v3.14.0
        GIT_SHALLOW TRUE
    )
  set(NATS_BUILD_STREAMING OFF CACHE BOOL "" FORCE)
  # Disable nats.c's own install rules when it is fetched as a
  # dependency. Its install target references both static and shared
  # artifacts, which fails if only one library type is built.
  set(NATS_BUILD_LIB_STATIC OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_LIB_SHARED ON CACHE BOOL "" FORCE)
  # nats.c's bundled tests require the static library; we do not need
  # them when consuming nats.c as a dependency.
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  # We do not need nats.c's examples; disabling them speeds up the build.
  set(NATS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(nats_c)
  # Restore BUILD_TESTING so the rest of this project can still enable
  # its own tests via CTest.
  unset(BUILD_TESTING CACHE)

  # nats.c exports its installed target as `cnats::nats`, but the in-tree
  # target produced by FetchContent is named `nats`. Expose a normalized
  # `cnats::nats` alias so the library can always link the same target name.
  if(TARGET nats AND NOT TARGET cnats::nats)
    add_library(cnats::nats ALIAS nats)
  elseif(TARGET nats_static AND NOT TARGET cnats::nats)
    add_library(cnats::nats ALIAS nats_static)
  endif()
endif()

# -------------------------------------------------------------------------
# glaze
# -------------------------------------------------------------------------
find_package(glaze CONFIG QUIET)
if(NOT glaze_FOUND)
  message(STATUS "glaze not found on system, fetching from GitHub")
  FetchContent_Declare(
        glaze
        GIT_REPOSITORY https://github.com/stephenberry/glaze.git
        GIT_TAG v9.1.0
        GIT_SHALLOW TRUE
    )
  # Install glaze alongside this project so that consumers of the
  # installed package can resolve glaze::glaze via find_dependency(glaze).
  set(glaze_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glaze)
endif()

