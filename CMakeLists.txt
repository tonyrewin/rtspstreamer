# CMakeLists.txt
#
# Build script for rtmpstreamer_tilde Pure Data external
#
# Usage:
# mkdir build
# cd build
# cmake ..
# make

cmake_minimum_required(VERSION 3.13)
project(rtmpstreamer_tilde C)

# Find the FFmpeg libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVUTIL REQUIRED libavutil)

# Set the compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wall -Wextra -Wno-unused-parameter -fPIC")

# Include directories
include_directories(
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    "/Applications/Pd-0.55-1.app/Contents/Resources/src"  # Replace with your actual Pd headers path
)

# Add the source file
add_library(rtmpstreamer_tilde MODULE rtmpstreamer~.c)

# Set library search paths
target_link_directories(rtmpstreamer_tilde PRIVATE
    ${AVFORMAT_LIBRARY_DIRS}
    ${AVCODEC_LIBRARY_DIRS}
    ${AVUTIL_LIBRARY_DIRS}
)

# Link the FFmpeg libraries
target_link_libraries(rtmpstreamer_tilde
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
)

# Set linker flags and output properties for Pd external
set_target_properties(rtmpstreamer_tilde PROPERTIES
    PREFIX ""                            # No prefix
    SUFFIX ".pd_darwin"                  # Suffix for macOS Pd external
    OUTPUT_NAME "rtmpstreamer~"           # Set the output name to rtmpstreamer~
    LIBRARY_OUTPUT_DIRECTORY "~/Documents/Pd/externals"  # Output directory
    LINK_FLAGS "-bundle -undefined dynamic_lookup"  # Linker flags for Pd
)

# Ensure the external is built as a module
set_target_properties(rtmpstreamer_tilde PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@loader_path"
)