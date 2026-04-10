# Quickstart: Phase 0 — Project Scaffold

**Date**: 2026-04-10
**Feature**: `001-phase0-scaffold`

## Prerequisites

Install build dependencies:

```bash
sudo apt install build-essential cmake pkg-config \
  librtmidi-dev libasound2-dev \
  libnng-dev nlohmann-json3-dev libtinyxml2-dev liblo-dev
```

Minimum versions:
- CMake >= 3.10
- GCC with C++17 support (GCC >= 7)
- librtmidi-dev >= 5.0

## Clone and Build

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/stagesoft/gradient-motion-engine.git
cd gradient-motion-engine

# If already cloned without submodules:
git submodule update --init --recursive

# Build
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

Expected output: two artifacts in `build/`:
- `libgradient_motion.a` — static library (empty placeholder modules in Phase 0)
- `gradient-motiond` — daemon executable

## Run

```bash
# Show help
./gradient-motiond --help

# Start with defaults (blocks until SIGTERM/SIGINT)
./gradient-motiond

# Start with custom options
./gradient-motiond --midi-port "Midi Through Port-0" --log-level debug --conf-path /etc/cuems

# Stop
kill -SIGTERM $(pidof gradient-motiond)
# or Ctrl+C if running in foreground
```

## Verify

```bash
# Check that the binary runs and exits on --help
./gradient-motiond --help && echo "OK: help works"

# Check that it starts and logs to journal
./gradient-motiond --log-level info &
PID=$!
sleep 1
journalctl -t "Cuems:GradientEngine" --no-pager -n 5
kill $PID

# Check that both build targets exist
ls -la libgradient_motion.a gradient-motiond
```

## What Phase 0 Does NOT Include

- No XML configuration file parsing (future phase)
- No MTC timecode reception (Phase 2)
- No NNG bus communication (Phase 3)
- No curve/gradient evaluation logic (Phase 1)
- No OSC output (Phase 4)
- No systemd service file (Phase 5)
