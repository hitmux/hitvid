# Native media bridge

`media_bridge.c` contains the narrow C ABI used by the native backend. It
feeds decoded RGB frames to Chafa's canvas API without creating image files.

Build the pinned static dependencies with:

```sh
make native
go build -tags native -o hitvid-native .
```

The script builds FFmpeg, GLib, and Chafa below `native/build/`. The default Go
binary continues to use the diskless process pipeline until the native build
is selected in the release build configuration. The bridge deliberately builds
Chafa without its command-line tools and image loaders because hitvid passes
RGB pixels directly.

FFmpeg is configured for file input and common video containers/codecs. Frame
rate sampling is done from timestamps and RGB conversion uses libswscale. GPL
and nonfree components are disabled.
The release process must ship the corresponding source and license notices for
the LGPL libraries and any statically linked objects.
