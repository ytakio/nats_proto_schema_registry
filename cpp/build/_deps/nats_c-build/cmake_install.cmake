# Install script for directory: /Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/nats_c-src

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/install")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/nats_c-build/libnats.pc")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/nats_c-build/src/cmake_install.cmake")
  include("/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/nats_c-build/examples/cmake_install.cmake")
  include("/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/nats_c-build/examples/getstarted/cmake_install.cmake")
  include("/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/nats_c-build/examples/docs/cmake_install.cmake")
  include("/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/nats_c-build/test/cmake_install.cmake")
  include("/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/nats_c-build/test/dylib/cmake_install.cmake")
  include("/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/nats_c-build/test/check_cpp/cmake_install.cmake")
  include("/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/nats_c-build/test/no_crash_on_exit/cmake_install.cmake")

endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/nats_c-build/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
