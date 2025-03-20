cmake_minimum_required(VERSION 3.10)
project(nvml_pub VERSION 0.1 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add custom module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Debug output to find libraries
message(STATUS "Looking for NVML library...")

# Find NVML - typically part of CUDA toolkit
find_library(NVML_LIBRARY
    NAMES nvidia-ml nvml
    PATHS /usr/local/cuda/targets/x86_64-linux/lib/stubs/
)

if(NVML_LIBRARY)
    message(STATUS "Found NVML library: ${NVML_LIBRARY}")
else()
    message(FATAL_ERROR "NVML library not found.")
endif()

# Debug output to find Mosquitto
message(STATUS "Looking for Mosquitto library and headers...")

find_library(MOSQUITTO_LIBRARY 
    NAMES mosquitto
    PATHS /usr/lib /usr/local/lib
)

find_path(MOSQUITTO_INCLUDE_DIR
    NAMES mosquitto.h
    PATHS /usr/include /usr/local/include
)

if(MOSQUITTO_LIBRARY)
    message(STATUS "Found Mosquitto library: ${MOSQUITTO_LIBRARY}")
else()
    message(FATAL_ERROR "Mosquitto library not found. Please install libmosquitto or specify the location.")
endif()

if(MOSQUITTO_INCLUDE_DIR)
    message(STATUS "Found Mosquitto headers at: ${MOSQUITTO_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "Mosquitto headers not found. Please install libmosquitto-dev package.")
endif()

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)

# Try to find iniparser using pkg-config first
pkg_check_modules(INIPARSER iniparser)

# If not found via pkg-config, use our custom Find module
if(NOT INIPARSER_FOUND)
  find_package(Iniparser REQUIRED)
endif()

# Include directories
include_directories(
    /usr/local/cuda/include
    /usr/local/lib
    ${MOSQUITTO_INCLUDE_DIR}
    ${INIPARSER_INCLUDE_DIRS}
)

# Add executable
add_executable(nvml_pub nvml_pub.c)

# Link libraries
target_link_libraries(nvml_pub 
    ${NVML_LIBRARY}
    ${MOSQUITTO_LIBRARY}
    ${INIPARSER_LIBRARIES}
    m  # Math library
    rt # Real-time library
)

# Compile definitions
target_compile_definitions(nvml_pub PRIVATE _GNU_SOURCE)

# Installation
install(TARGETS nvml_pub DESTINATION bin)
install(FILES nvml_pub.conf DESTINATION /etc)

# Output configuration summary
message(STATUS "NVML_PUB Configuration:")
message(STATUS "  Mosquitto include dirs: ${MOSQUITTO_INCLUDE_DIRS}")
message(STATUS "  Mosquitto libraries: ${MOSQUITTO_LIBRARIES}")
message(STATUS "  Iniparser include dirs: ${INIPARSER_INCLUDE_DIRS}")
message(STATUS "  Iniparser libraries: ${INIPARSER_LIBRARIES}")
