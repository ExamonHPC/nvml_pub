# FindIniparser.cmake
# Finds the iniparser library
#
# This will define the following variables:
#   INIPARSER_FOUND        - True if iniparser is found
#   INIPARSER_INCLUDE_DIRS - Include directories for iniparser
#   INIPARSER_LIBRARIES    - Libraries for iniparser
#

# Use pkg-config if available
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_INIPARSER QUIET iniparser)
endif()

# Find include directory
find_path(INIPARSER_INCLUDE_DIR
  NAMES iniparser.h
  PATHS ${PC_INIPARSER_INCLUDE_DIRS}
  PATH_SUFFIXES iniparser
)

# Find library
find_library(INIPARSER_LIBRARY
  NAMES iniparser
  PATHS ${PC_INIPARSER_LIBRARY_DIRS}
)

# Set version from pkg-config if available
set(INIPARSER_VERSION ${PC_INIPARSER_VERSION})

# Handle standard find_package arguments
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Iniparser
  REQUIRED_VARS INIPARSER_LIBRARY INIPARSER_INCLUDE_DIR
  VERSION_VAR INIPARSER_VERSION
)

# Set output variables
if(INIPARSER_FOUND)
  set(INIPARSER_LIBRARIES ${INIPARSER_LIBRARY})
  set(INIPARSER_INCLUDE_DIRS ${INIPARSER_INCLUDE_DIR})
endif()

# Mark as advanced
mark_as_advanced(
  INIPARSER_INCLUDE_DIR
  INIPARSER_LIBRARY
)
