# rtspstreamer~

A Pure Data external that streams audio via RTSP to a remote server.

## Overview

`rtspstreamer~` allows you to stream audio directly from Pure Data (Pd) to an RTSP server. It takes an audio signal as input and streams it to a specified RTSP URL using FFmpeg libraries.

## Features

- Streams audio over RTSP protocol.
- Uses FFmpeg for encoding and streaming.
- Simple integration with Pd patches.
- Configurable output URL (set at object creation).

## Dependencies

- **Pure Data**: Installed on your system.
- **FFmpeg Libraries**:
  - `libavformat`
  - `libavcodec`
  - `libavutil`
- **CMake**: For building the external.

## Installation

### 1. Install Dependencies

Ensure that you have the necessary development libraries and headers.

#### On Debian/Ubuntu:

```bash
sudo apt-get install puredata-dev libavformat-dev libavcodec-dev libavutil-dev cmake
```

#### On macOS using Homebrew:

```sh
brew install pure-data ffmpeg cmake
```

### 2. Download the Source Code

Clone or download the repository containing `rtspstreamer~.c` and `CMakeLists.txt`.

### 3. Build the External

Create a build directory and compile the external using **CMake** and **make**.

```sh
cd rtspstreamer
mkdir build
cd build
cmake ..
make
```

### 4. Install the External

