cmake_minimum_required(VERSION 3.10)
project(nvml_pub C)

# Set C standard
set(CMAKE_C_STANDARD 99)

# Find CUDA and NVML
find_package(CUDA REQUIRED)

# Include CUDA headers
include_directories(${CUDA_INCLUDE_DIRS})

# Find Mosquitto library
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)

# Include Mosquitto headers
include_directories(${MOSQUITTO_INCLUDE_DIRS})

# Add executable
add_executable(nvml_pub nvml_pub.c)

# Link libraries
target_link_libraries(nvml_pub
    ${CUDA_LIBRARIES}
    ${MOSQUITTO_LIBRARIES}
)

# Set CUDA library path (if needed)
if (CUDA_LIBRARIES)
    link_directories(${CUDA_LIBRARY_DIRS})
endif()

# Set Mosquitto library path (if needed)
if (MOSQUITTO_LIBRARIES)
    link_directories(${MOSQUITTO_LIBRARY_DIRS})
endif()