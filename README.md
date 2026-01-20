# GrooveBox UI (Qt6 Widgets)

A Qt6 Widgets groovebox / drum machine UI built for 1280x720 displays.

## Build (Raspberry Pi / Linux)

```sh
sudo apt update
sudo apt install -y git cmake g++ ffmpeg libasound2-dev qt6-base-dev qt6-base-dev-tools qt6-multimedia-dev
mkdir build
cd build
cmake ..
cmake --build .
```

## Run

```sh
./GrooveBoxUI
```

Linux framebuffer (no X/Wayland):

```sh
sudo QT_QPA_PLATFORM=linuxfb ./GrooveBoxUI
```

Notes:
- Uses custom paintEvent rendering for all UI sections.
- Pitch/stretch processing uses `ffmpeg`; without it those controls are ignored.
- Sample browser reads WAV/MP3 from USB mounts under `/media` or `/run/media`.
