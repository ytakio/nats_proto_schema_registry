# Install script for directory: /Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/glaze-src

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

if(CMAKE_INSTALL_COMPONENT STREQUAL "glaze_Development" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE DIRECTORY FILES "/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/glaze-src/include/")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "glaze_Development" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/glaze" TYPE FILE FILES
    "/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/glaze-src/cmake/glaze-asio.cmake"
    "/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/glaze-src/cmake/FindAsio.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "glaze_Development" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/glaze" TYPE FILE FILES "/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/glaze-build/glazeConfig.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "glaze_Development" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/glaze" TYPE FILE FILES "/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/glaze-build/glazeConfigVersion.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "glaze_Development" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/glaze/glazeTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/glaze/glazeTargets.cmake"
         "/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/glaze-build/CMakeFiles/Export/4060b7be0bf29cefc5b631f973b7eb37/glazeTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/glaze/glazeTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/glaze/glazeTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/glaze" TYPE FILE FILES "/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/glaze-build/CMakeFiles/Export/4060b7be0bf29cefc5b631f973b7eb37/glazeTargets.cmake")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/t-yamaoka/work/nats/nats_proto_schema_registry/cpp/build/_deps/glaze-build/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
