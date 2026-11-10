#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "cnats::nats" for configuration "Release"
set_property(TARGET cnats::nats APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(cnats::nats PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libnats.3.14.0.dylib"
  IMPORTED_SONAME_RELEASE "@rpath/libnats.3.14.dylib"
  )

list(APPEND _cmake_import_check_targets cnats::nats )
list(APPEND _cmake_import_check_files_for_cnats::nats "${_IMPORT_PREFIX}/lib/libnats.3.14.0.dylib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
