# hitvid: High-Performance Terminal Video Player

[![Go Version](https://img.shields.io/badge/go-1.18+-blue.svg)](https://golang.org)
[![License: AGPLv3](https://img.shields.io/badge/License-AGPLv3-yellow.svg)](https://opensource.org/licenses/AGPLv3)

`hitvid` is a high-performance, feature-rich video player designed to run directly in your terminal. It leverages the power of **ffmpeg** for video processing and **chafa** for superior character-based rendering to deliver a smooth playback experience. It features a sophisticated concurrency model for pre-rendering frames, ensuring that playback is not hindered by on-the-fly processing.

We've completely rewritten hitvid in Go, achieving a performance leap! We've leveraged multiple technologies to accelerate video rendering. We now support Linux, macOS, Windows, and other platforms!

However, we still keep the previous shell version in the `/old` directory, leaving it as a last resort for platforms that are really incompatible (although there are almost no such platforms).

## [Hitmux Official Website https://hitmux.org](https://hitmux.org)

## Key Features

*   **High-Performance Playback**: Utilizes a multi-threaded pipeline to decode and render frames in parallel, ahead of playback.
*   **Automatic Playlist Generation**: Automatically detects and queues all supported video files (`.mp4`, `.mkv`, `.mov`, etc.) in the target video's directory.
*   **Rich Playback Controls**: Offers intuitive keyboard shortcuts for pausing, seeking, speed adjustment, and navigating the playlist.
*   **Highly Customizable Rendering**: Provides command-line options to control FPS, character sets (`symbols`), color modes, dithering algorithms, and output dimensions.
*   **Efficient Synchronization**: Employs condition variables (`sync.Cond`) to eliminate busy-waiting, ensuring minimal CPU usage while buffering or paused.
*   **Graceful Cancellation**: Uses `context.Context` throughout the application for clean and immediate shutdown of all processes and goroutines.
*   **Terminal UI**: Manages the terminal state, hiding the cursor and using an alternate screen buffer for a clean viewing experience that restores the terminal on exit.

## Dependencies

Before using `hitvid`, you must have the following command-line tools installed and accessible in your system's `PATH`:

1.  **FFmpeg**: The core engine for decoding and extracting frames from video files.
    *   **APT or YUM Installation** `sudo apt install ffmpeg` or `sudo yum install ffmpeg` 
    *   **Website & Installation**: [ffmpeg.org](https://ffmpeg.org/download.html)
2.  **Chafa**: The powerful utility for converting images into high-quality terminal character art.
    *   **Website & Installation**: [hpjansson.org/chafa](https://hpjansson.org/chafa/)
    *   **APT or YUM Installation** `sudo apt install chafa` or `sudo yum install chafa` 

## Installation & Usage

#### 1. Install Dependencies
First, ensure you have installed Go (version 1.18+), ffmpeg, and chafa.

#### 2. Get the Source
Clone the repository to your local machine (note: example URL).
```bash
git clone https://github.com/hitmux/hitvid.git
cd hitvid
```

#### 3. Run Directly
You can run the program directly using `go run`. This is useful for quick plays without needing to build a binary.

```bash
# Basic usage
go run hitvid.go /path/to/your/video.mp4

# Advanced usage with custom rendering options
go run hitvid.go -fps 24 -colors full -symbols block /path/to/your/video.webm
```

#### 4. Build the Binary
For a permanent and faster-launching command, build the executable.
```bash
go build -o hitvid hitvid.go
```
Then you can run the compiled binary from anywhere:
```bash
./hitvid -w 120 -h 40 /path/to/another/video.mkv
```

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

`hitvid` is not a simple script; it's a concurrent application designed for efficiency. Its architecture can be broken down into several key areas:

#### 1. The Rendering Pipeline (Producer-Consumer Model)

The core of the player is a multi-stage pipeline that processes video frames asynchronously.

1.  **Frame Extractor (`ffmpeg`)**: An `ffmpeg` process is spawned to read the video file, decode it, and save individual frames as JPG images into a temporary directory. This is the first producer.

2.  **Job Dispatcher (Goroutine)**: A dedicated goroutine runs in the background, watching the temporary directory for new frame images. As soon as a frame appears, it creates a `renderJob` and pushes it into a buffered channel (`jobs`). This decouples frame extraction from frame rendering.

3.  **Frame Renderers (`chafa` Workers)**: A pool of consumer goroutines (the number is set by `-threads`) concurrently pulls `renderJob`s from the `jobs` channel. Each worker invokes `chafa` on its assigned frame image to convert it into a pre-rendered string of terminal characters. The result is stored in the `renderedFrames` slice at the correct index.

4.  **Playback Loop (Main Goroutine)**: The final consumer is the main playback loop. It attempts to play `currentFrameIndex`. If the frame isn't rendered yet (`renderedFrames[currentFrameIndex]` is `nil`), it waits efficiently instead of spinning the CPU.

#### 2. Advanced Synchronization

Managing the state between these concurrent parts is critical.

*   **`sync.Mutex (stateMutex)`**: A global mutex protects shared state variables like `isPaused`, `currentFrameIndex`, `totalFrames`, `lastRenderedFrame`, and user input actions. This prevents race conditions when, for example, the user pauses playback at the same time a new frame is rendered.

*   **`sync.Cond (frameReadyCond)`**: This is the key to efficient waiting. The playback loop uses `frameReadyCond.Wait()` when it needs a frame that hasn't been rendered yet. This puts the goroutine to sleep, consuming no CPU. When a rendering worker finishes a frame, it calls `frameReadyCond.Broadcast()`, which wakes up the playback loop to re-check if its required frame is now available. This is vastly more efficient than a `time.Sleep()` loop.

#### 3. State and Lifecycle Management

*   **`context.Context`**: A `context.WithCancel` is created for each video played. This `context` is passed down to every goroutine and `exec.Command` related to that video. When the user quits, skips to the next video, or the video finishes, `cancel()` is called. This sends a cancellation signal down the entire chain, gracefully terminating `ffmpeg`, any running `chafa` processes, and all associated goroutines, ensuring no orphaned processes are left behind.

*   **Main Control Loop**: The `main()` function contains the top-level control loop. It manages the playlist, handles transitions between videos (`next`, `prev`), and re-initializes the state for each new video. This outer loop is responsible for the application's overall lifecycle, while the `playVideo` function manages the lifecycle of a single video playback session.

## License

This project is licensed under the AGPLv3 License.

## Acknowledgments

*   This program would not be possible without the incredible work of the **FFmpeg** team.
*   The beautiful terminal output is thanks to the **Chafa** library by Hans-Peter Jansson.
