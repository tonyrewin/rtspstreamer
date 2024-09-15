# CMakeLists.txt
#
# Build script for rtspstreamer Pure Data external
#
# Usage:
# mkdir build
# cd build
# cmake ..
# make

cmake_minimum_required(VERSION 3.13)
project(rtspstreamer C)

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
add_library(rtspstreamer MODULE rtspstreamer.c)

# Set library search paths
target_link_directories(rtspstreamer PRIVATE
    ${AVFORMAT_LIBRARY_DIRS}
    ${AVCODEC_LIBRARY_DIRS}
    ${AVUTIL_LIBRARY_DIRS}
)

# Link the FFmpeg libraries
target_link_libraries(rtspstreamer
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
)

# Set linker flags for Pd external
set_target_properties(rtspstreamer PROPERTIES
    PREFIX ""
    SUFFIX ".pd_darwin"  # For macOS
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    LINK_FLAGS "-bundle -undefined dynamic_lookup"
)

# Ensure the external is built as a module
set_target_properties(rtspstreamer PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@loader_path"
)