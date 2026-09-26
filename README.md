<img src="docs/images/birds-visual-1.png" alt="Rill World: birds against a blue sky" width="800">

# Rill World

**rill** /rɪl/ *noun*: a small stream or a tiny, shallow channel cut into soil by running water.

A generative environment instrument for the **M5Stack StickS3**. Field recordings of rain, birds, insects, ocean and cave ambiences move through slow filters and effects, accompanied by particle visuals. Tap for a new sample. Shake for a new environment.

[Play Rill World](https://rillsound.com/world) · [Get on M5Burner](https://burner.m5stack.com/firmware/2102132303056334850) · [Build and install](#build-and-install)

**Rill family:** [Voice](https://github.com/bruceblay/rill-voice) · [Synth](https://github.com/bruceblay/rill-synth) · [Mallet](https://github.com/bruceblay/rill-mallet) · [World](https://github.com/bruceblay/rill-world) · [Drums](https://github.com/bruceblay/rill-drums) · [Rill Sound](https://rillsound.com)

## Visuals

Five environments, each with its own particle movement and ground colour. Tap changes the ink; shake changes the environment.

| Birds | Rain |
| --- | --- |
| ![Birds](docs/images/birds-visual-1.png) | ![Rain](docs/images/rain-visual-1.png) |
| **Insects** | **Ocean** |
| ![Insects](docs/images/insects-visual-1.png) | ![Ocean](docs/images/ocean-visual-1.png) |
| **Cave** | |
| ![Cave](docs/images/cave-visual-1.png) | |

Actual 240 × 135 renderer captures. Birds flap across a blue sky; rain forms expanding ripples; insects hover and dart; ocean bubbles rise; cave droplets fall. The animation and bar highlights are best seen [in motion](https://rillsound.com/world). See the [full palette gallery](docs/VISUALS.md) for all ink variants.

## Play

| Gesture | Action |
| --- | --- |
| Front button: tap | Pick a new sample and character within the current bank, change the visual, and play |
| Front button: hold for about 0.65 seconds | Fade sound out or in; the loop continues while quiet |
| Side button: tap | Cycle volume (it starts at the quietest step) and show the data view for four seconds |
| Side button: hold | Slow the whole ensemble by 4 BPM, starting on the bar after next; below 52 it comes round to 100 |
| Shake | Cross into a different bank -- a different environment, a different visual palette and drift |

The data view shows the current bank, recording, generation, tempo, bar count, volume and battery estimate.

## Sound

Five banks of field recordings: **Rain, Birds, Insects, Ocean and Cave**. Each bank moves through a shuffled pool of samples, avoiding an immediate repeat. Rain ranges from a steady wash to percussive surfaces; Birds and Insects gather outdoor ambiences; Ocean combines waves and underwater sound; Cave uses composed drip-and-reverb recordings. See [sources and licenses](docs/SOURCES.md).

A slow, bar-synced lowpass sweep gives the loops movement. Five self-clearing effects (Pitch Wobble, Delay Throw, Crush, Reverb and Smear) briefly reshape the sound, with gentler settings for Rain and Ocean. Cave plays at 65% speed, extending its three-second recordings to about 4.6 seconds.

Playback state is not saved across restarts. Near a Rill Voice, Synth, Mallet or Drums running its ensemble radio, World follows their shared tempo and bar line over ESP-NOW, leaving the clock to them, so its sweep breathes with them, and a new texture comes in on the next bar line. The same Ensemble is in [Rill Sound](https://rillsound.com/ensemble).

## Hardware

Supported and tested: **M5Stack StickS3**, with ESP32-S3, 8 MB flash, display, IMU and built-in speaker. Other ESP32 boards and earlier M5Stick models are not supported by this configuration.

The PlatformIO board name is `esp32-s3-devkitc-1`; the project supplies the StickS3 memory settings and uses M5Unified for board peripherals. Flash is partitioned as a single factory app slot (`partitions_field.csv`), not the usual two-slot OTA layout, so the embedded clips fit -- this device is flashed by USB each time, not updated over the air. The partition has been grown twice now (~7 MB, then ~7.6 MB, now ~7.87 MB) to fit Insects' and then Cave's clips, -- the practical ceiling on an 8 MB chip. The ensemble radio's WiFi stack (~390 KB) then displaced the rain-on-wheelbarrow clip, leaving the app ~98.5% full (~120 KB free). Anything more will need to trim or drop an existing clip rather than grow further.

## Build and install

Install Python 3.11 or later, then run these commands from the repository root:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements-dev.txt
pio run
```

On Windows, activate with `.venv\Scripts\activate` instead. PlatformIO downloads the pinned platform and library dependencies on the first build.

Connect the StickS3 with a USB data cable, locate its port with `pio device list`, then install:

```sh
python tools/flash.py --port YOUR_DEVICE_PORT
```

Flashing replaces the firmware currently on the device. The script builds and uploads, then applies the watchdog reset used successfully during Rill's development; a normal RTS reset can leave this board in download mode.

To observe diagnostics:

```sh
pio device monitor --port YOUR_DEVICE_PORT --baud 115200
```

Close the monitor before another upload.

## Develop without hardware

Host tools use the same C++ engine and visual code as the firmware. A C++17 compiler is required.

```sh
python tools/test.py
mkdir -p build
c++ -std=c++17 -O2 tools/render.cpp -o build/render
build/render build/rain.wav 60 42
c++ -std=c++17 -O2 tools/visual_preview.cpp -o build/visual_preview
build/visual_preview build/preview.ppm 23 0
```

The audio arguments are output path, seconds, and optional seed. The visual arguments are output path, seed, and optional regenerate-once flag. Audio output is mono 32 kHz / 16-bit WAV; visual output is PPM. For host address/undefined-behavior checks, run `python tools/test.py --sanitize` with a compatible compiler. Set `CXX` to choose a compiler.

`tools/embed_samples.py` converts mono 16-bit 32 kHz WAV loops into `src/Samples.h`; see that file's docstring and [docs/SOURCES.md](docs/SOURCES.md) before replacing or adding a clip.

Tests cover five simulated minutes of playback per seed, bounded output, a jump/discontinuity bound across crossfades and the loop point, seed reproducibility, per-bank texture coverage, that tap never crosses a bank boundary and shake always can, fade/resume, and shake gesture recognition. They do not replace listening or checking the physical screen.

## Project layout

- `src/Field.h` -- sample playback, bank ranges, bar-synced sweep and punch-in effects
- `src/Samples.h` -- generated PCM data; see `tools/embed_samples.py`
- `src/Weather.h` -- the ambient particle visual, one palette set and drift kind per bank
- `src/main.cpp` -- audio, display, buttons and motion tasks
- `src/ShakeDetector.h` -- gesture recognition, shared with Rill
- `tools/` -- portable tests, auditions, previews, sample embedding and flashing
- `tests/` -- host verification
- `docs/SOURCES.md` -- source recordings and licenses for the embedded clips

## Credits and license

Created by Bruce Blay. Developed through iterative on-device listening and viewing, with Claude assisting implementation.

Rill World follows Rill's parent project Pocket Radio's **GPL-3.0-or-later** license. See [LICENSE](LICENSE). The embedded field recordings are separately licensed CC0 1.0; see [docs/SOURCES.md](docs/SOURCES.md).
