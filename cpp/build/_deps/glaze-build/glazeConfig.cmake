
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was install-config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

include(CMakeFindDependencyMacro)

# Handle EETF format dependency (Erlang)
# The FindErlang module is embedded directly to avoid file collisions with other projects
set(_glaze_EETF_FORMAT "OFF")
if(_glaze_EETF_FORMAT)

    # glaze::glaze links Erlang::EI unconditionally once built with EETF, so a
    # consumer that cannot resolve Erlang cannot use this package at all. Report
    # it as a failed find_package, which REQUIRED turns into a clear error and a
    # plain find_package can test with glaze_FOUND. Without this the missing
    # Erlang::EI target only surfaces from glazeTargets.cmake at generate time,
    # after configure has already reported success.
    if(NOT Erlang_FOUND)
        set(glaze_FOUND FALSE)
        set(glaze_NOT_FOUND_MESSAGE
            "glaze was built with glaze_EETF_FORMAT=ON but Erlang/OTP's erl_interface \
library was not found. Install Erlang/OTP, or point the build at it with \
ERLANG_EI_LIB and ERLANG_EI_INCLUDE.")
        unset(_glaze_EETF_FORMAT)
        return()
    endif()
endif()
unset(_glaze_EETF_FORMAT)

# Handle SSL dependency (OpenSSL)
set(_glaze_ENABLE_SSL "OFF")
if(_glaze_ENABLE_SSL)
    find_dependency(OpenSSL REQUIRED)
endif()
unset(_glaze_ENABLE_SSL)

include("${CMAKE_CURRENT_LIST_DIR}/glazeTargets.cmake")

# Make the Asio backend selector available to networking consumers. This only
# declares the glaze_USE_BUNDLED_ASIO option and the glaze_setup_asio() function;
# nothing is resolved until the consumer calls glaze_setup_asio() and links
# glaze::asio. Doing so picks the same backend and sets GLZ_USE_BOOST_ASIO exactly
# as the in-tree build, so glaze/ext/glaze_asio.hpp can never disagree with the
# linked Asio (issue #2599). Guarded so a source build without the installed
# module (e.g. add_subdirectory) still configures.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/glaze-asio.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/glaze-asio.cmake")
endif()
