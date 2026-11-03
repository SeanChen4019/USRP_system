# External components

The communication source is C++17. Standard C++/OS socket APIs are used by the core executable.

The local `tools/ffmpeg/` directory contains FFmpeg 9.0.2 essentials Windows build downloaded from:

https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip

Archive SHA256: `60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba`.

The bundled distribution's licenses/documentation remain in its extracted folder. This FFmpeg build includes GPL components such as libx264. FFmpeg is used as a separate encoder/player process, not presented as project-authored C++ code.

Optional hardware integration requires a separately installed Ettus UHD SDK. Reference APIs:

https://github.com/EttusResearch/uhd

No UHD firmware image or driver was installed or changed during this implementation.
