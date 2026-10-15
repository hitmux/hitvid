# hitvid: High-Performance Terminal Video Player

[![Go Version](https://img.shields.io/badge/go-1.18+-blue.svg)](https://golang.org)
[![License: AGPLv3](https://img.shields.io/badge/License-AGPLv3-yellow.svg)](https://opensource.org/licenses/AGPLv3)

`hitvid` is a high-performance terminal video player. **v1.2.2 official Linux amd64 artifacts are fully standalone at runtime**: FFmpeg and Chafa are linked into the executable and invoked through native APIs, with no separate `ffmpeg`, `ffprobe`, `chafa`, or media shared-library installation required.

The release backend decodes frames with the embedded FFmpeg libraries, converts them to RGB in memory, and renders them through the embedded Chafa canvas API. A compatibility development backend remains available for unsupported targets.

However, we still keep the previous shell version in the `/old` directory, leaving it as a last resort for platforms that are really incompatible (although there are almost no such platforms).

## [Hitmux Official Website https://hitmux.com](https://hitmux.com)

## Key Features

*   **High-Performance Playback**: Utilizes a multi-threaded pipeline to decode and render frames in parallel, ahead of playback.
*   **Automatic Playlist Generation**: Automatically detects and queues all supported video files (`.mp4`, `.mkv`, `.mov`, etc.) in the target video's directory.
*   **Rich Playback Controls**: Offers intuitive keyboard shortcuts for pausing, seeking, speed adjustment, and navigating the playlist.
*   **Highly Customizable Rendering**: Provides command-line options to control FPS, character sets (`symbols`), color modes, dithering algorithms, and output dimensions.
*   **Efficient Synchronization**: Employs condition variables (`sync.Cond`) to eliminate busy-waiting, ensuring minimal CPU usage while buffering or paused.
*   **Graceful Cancellation**: Uses `context.Context` throughout the application for clean and immediate shutdown of all processes and goroutines.
*   **Terminal UI**: Manages the terminal state, hiding the cursor and using an alternate screen buffer for a clean viewing experience that restores the terminal on exit.

## Runtime Dependencies

The official **Linux amd64 v1.2.2 release has no external runtime software dependencies**. Download the raw binary or install the `.deb` and run it directly.

Building from source requires Go, a C toolchain, Meson, Autotools, and the development libraries used to produce the fully static binary. These are build-time requirements only.

## Installation & Usage

#### 1. Official standalone release
Use `hitvid-linux-amd64` or the `hitvid_*_amd64.deb` attached to the GitHub Release. No FFmpeg or Chafa package installation is required.

#### 2. Get the Source
Clone the repository to your local machine (note: example URL).
```bash
git clone https://github.com/hitmux/hitvid.git
cd hitvid
```

#### 3. Build the standalone binary

```bash
make build
```

This produces `dist/hitvid-linux-amd64`, runs the native decode/render tests, and verifies that the executable is fully static and can start with an empty `PATH`.

```bash
./dist/hitvid-linux-amd64 -w 120 -h 40 /path/to/another/video.mkv
```

For development on unsupported native targets, `make compat` builds the external-command compatibility backend.

### Debian package

Pull requests and releases build an `amd64` Debian package in a Debian 13 container. The package installs the same fully static standalone executable at `/usr/bin/hitvid` and declares no `ffmpeg` or `chafa` dependency. Tagged `v*` builds attach both the raw standalone binary and the `.deb` to the GitHub Release.

## Command-Line Options

The player's behavior can be fine-tuned with the following flags:

| Flag              | Description                                                                 | Default Value            |
| ----------------- | --------------------------------------------------------------------------- | ------------------------ |
| `-video <path>`   | Path to the video file. Can also be given as the last positional argument.  | *(None)*                 |
| `-fps <integer>`  | Frame rate for video extraction and playback.                               | `15`                     |
| `-symbols <str>`  | Symbol set for chafa (e.g., `block`, `ascii`, `legacy`, `braille`).         | `block`                  |
| `-colors <str>`   | Color mode for chafa (e.g., `none`, `16`, `256`, `full`).                   | `256`                    |
| `-dither <str>`   | Dithering algorithm for chafa (e.g., `none`, `ordered`, `diffusion`).       | `ordered`                |
| `-w <integer>`    | Render width in terminal columns.                                           | Terminal width           |
| `-h <integer>`    | Render height in terminal rows.                                             | Terminal height - 1      |
| `-scale <mode>`   | Scaling mode: `fit`, `fill`, or `stretch`.                                  | `fit`                    |
| `-threads <int>`  | Number of parallel threads to use for rendering frames with Chafa.          | `4`                      |
| `-help`           | Display a detailed help message and exit.                                   | `false`                  |

## Playback Controls

Control playback with these keyboard shortcuts:

| Key(s)               | Action                                               |
| -------------------- | ---------------------------------------------------- |
| `Q` / `Ctrl+C`       | Exit the program immediately.                        |
| `Spacebar`           | Pause or resume playback.                            |
| `+` (Plus)           | Increase playback speed.                             |
| `-` (Minus)          | Decrease playback speed.                             |
| `→` (Right Arrow)    | Seek forward 5 seconds.                              |
| `←` (Left Arrow)     | Seek backward 5 seconds.                             |
| `↓` (Down Arrow)     | Play the next video in the directory playlist.       |
| `↑` (Up Arrow)       | Play the previous video in the directory playlist.   |

## Architecture Deep Dive

The v1.2.2 release path is an in-process producer/consumer pipeline:

1. **Embedded FFmpeg decoder** reads the video container and decodes frames through libavformat/libavcodec.
2. **libswscale** converts decoded frames to RGB in memory.
3. **Embedded Chafa workers** render RGB frames directly through the Chafa canvas API.
4. A bounded frame store applies backpressure, keeping memory usage independent of total video duration.
5. Playback consumes rendered frames while keyboard events control pause, seek, speed, and playlist navigation.

The native Linux backend currently supports `-scale fit`; `fill` and `stretch` are available in the compatibility backend.

No release-path stage creates frame files or spawns `ffmpeg`, `ffprobe`, or `chafa` processes.

## Standalone build

`make build` downloads the pinned FFmpeg, GLib, and Chafa source releases, builds static libraries under `native/build/`, runs native tests, links a fully static Linux amd64 executable, and runs `scripts/verify-standalone.sh`.

```bash
make build
make verify
```

`make native` builds only the pinned native libraries. `make compat` remains available for development builds that use external FFmpeg/Chafa commands.

## License

This project is licensed under the AGPLv3 License.

## Acknowledgments

*   This program would not be possible without the incredible work of the **FFmpeg** team.
*   The beautiful terminal output is thanks to the **Chafa** library by Hans-Peter Jansson.
