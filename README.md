# ESP32_S3_LasVegasSpereEyes

Las Vegas Sphere style eyeballs on the Waveshare ESP32-S3-Touch-LCD-2.8C (round 480x480 ST7701S panel).
Each board is one eye: a shaded yellow ball with a big white cartoon eye that looks around and blinks.
Two boards side by side make the face, kept in step over ESP-NOW. Later: follow people with an HLK-LD2450 radar, and keep the two
boards in step over ESP-NOW.

![Preview](docs/preview.png)

## Build and flash

ESP-IDF v5.5.5 (WSL Ubuntu, `~/esp/esp-idf-5.5.5`, `./install.sh esp32s3`). No component downloads needed.

```bash
. ~/esp/esp-idf-5.5.5/export.sh
idf.py -B ~/esp/build-sphere-eyes set-target esp32s3 build
```

Flash from Windows over the USB-C UART port (CH343):

```bash
py -m esptool --chip esp32s3 -p COM6 -b 921600 write-flash "@flash_args"
```

Run that from the build folder. The boot log prints the frame rate every 10 s (115200 baud).

## Settings

| File | What |
|---|---|
| `components/board/include/board_config.h` | Pins, panel timing, backlight level |
| `main/app_config.h` | Which eye this board is (`APP_EYE_IS_LEFT`), frame rate, radar pins and on/off, ESP-NOW role |
| `main/secrets.h` | ESP-NOW keys and peer MAC. Gitignored: copy `main/secrets_example.h` |

Backlight: GPIO6 PWM (5 kHz, 13-bit) drives an N-MOSFET on the LED cathode. The Waveshare demo boots at 70 %.
This build defaults to 50 % for all-day running (`BOARD_BACKLIGHT_DEFAULT_PERCENT`).

## Layout

```
components/board      Panel bring-up (TCA9554 expander, ST7701S SPI init, RGB panel, 2 PSRAM frame buffers), backlight
components/eyes       Renderer, animator, and the three plug-in points:
  styles/             eye_style_t: background, eye placement, how one eye is drawn
  blink/              blink_effect_t: time 0..1 -> openness of each eye
  gaze/               gaze_source_t: where to look (idle wander)
components/ld2450     HLK-LD2450 UART driver + gaze source (off until wired)
components/eye_sync   ESP-NOW leader/follower link + gaze source (off by default)
main                  Picks style, eye side and gaze sources, runs the 30 fps render task
```

Rendering redraws only the area around the eyes each frame (anti-aliased ellipses in an internal-RAM strip),
then page-flips at the panel's frame boundary, so there is no tearing.

- New look: add `styles/style_x.c` with an `eye_style_t`, declare it in `eye_style.h`, append it to `stylePlaylist`
  in `app_main.c`. Built in: `eyeStyleSphere` (one eye per screen), `eyeStyleSphereFace` (both eyes on one screen)
  and `eyeStyleSauron` (flaming eye with a slit pupil, Halloween).
- Design rotation: the master shows each design in `stylePlaylist` for `APP_STYLE_SECONDS` (15 s) and sends the
  slave which one is up, so both screens switch together. Flash both boards with the same playlist.
  All designs are drawn in code (no stored images), so each one costs only a few KB of flash.
- New blink: add a `blink_effect_t` in `blink/blink_effects.c` and append it to `blinkEffectTable` (append only: the index goes over ESP-NOW).
- New input: implement `gaze_source_t` and add it to `gazeSources` in `app_main.c`. Higher in the list wins.

## Radar (LD2450)

Not wired yet. Plan: radar TX to GPIO19, radar RX to GPIO20, 5 V and GND from the 12-pin header, then set
`APP_RADAR_ENABLED 1`. GPIO43/44 reach the header only with the USB-C port unplugged, so they are not used.
Untested on hardware: if the eyes look the wrong way, set `APP_RADAR_MIRROR_X true`.

## Two boards (ESP-NOW)

This folder builds the **master** (viewer's left eye). `eye_slave/` builds the **slave** (right eye) from the
same components; only the eye side and ESP-NOW role differ, set in each folder's `CMakeLists.txt`.
Both boards can be plugged in at once (each gets its own COM port).

The master looks around and blinks on its own and broadcasts its gaze (20 Hz) and every blink. The slave holds
center and never blinks by itself; it follows the master and eases back to center if the master goes quiet.
Broadcast and unencrypted by default, so no MAC or key is needed. Tested on two boards.
