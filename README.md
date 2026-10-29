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
  `eyeStyleSauron` (flaming eye with a slit pupil, Halloween), `eyeStyleSphereDrunk` (bloodshot Sphere eye, Halloween), and the wide almond eyes `eyeStyleGrinch` and
  `eyeStyleElf` (the white is 3/4 of the screen wide; the iris does most of the looking around).
- Design rotation: the master shows each design in `stylePlaylist` for `APP_STYLE_SECONDS` (15 s) and sends the
  slave which one is up, so both screens switch together. Flash both boards with the same playlist.
  All designs are drawn in code (no stored images), so each one costs only a few KB of flash.
- New blink: add a `blink_effect_t` in `blink/blink_effects.c` and append it to `blinkEffectTable` (append only: the index goes over ESP-NOW).
- New input: implement `gaze_source_t` and add it to `gazeSources` in `app_main.c`. Higher in the list wins.

## Radar (LD2450)

Master board only. Wiring: radar RX to GPIO19 (D-), radar TX to GPIO20 (D+), 5 V and GND from the 12-pin header.
GPIO43/44 reach the header only with the USB-C port unplugged, so they are not used. GPIO19/20 are the chip's
native USB pins, so `sdkconfig.defaults` turns native USB off (logs and flashing use the CH343 port).
**Mounting:** module upright (long 44 mm edge vertical, the large copper pour at the top), antenna side (gold
patches) facing the people. Lying flat it only sees a narrow slice to one side, which is what went wrong on
2026-10-25. Per the Hi-Link manual: ideally 1.5 to 2 m high, nothing metal near the antenna, and a cover
(the pumpkin) of thin, even plastic with no metallic paint, about 12 or 19 mm in front of the antenna face.

The log prints a radar line every second (`APP_RADAR_LOG_MS`): `0 bytes` means nothing reaches GPIO20 (wiring,
power or ground), bytes with `0 good` frames means a wrong baud rate or noise, about 10 good frames a second means
it works, then where the eyes are aiming. With `APP_RADAR_TRACE 1` every frame is printed as an `RT` line for
`tools/radar_capture.py`, which records a walk and prints the frame rate, each second's nearest target and bearing,
the widest bearing on each side, still-target jitter and frozen repeats. With `--slave COM7` it also measures how
far apart the two eyes' blinks start.

How the eyes pick who to follow (`components/ld2450/ld2450.c`, all set in `main/app_config.h`):
- Only targets between `APP_RADAR_MIN_RANGE_M` and `APP_RADAR_MAX_RANGE_M`.
- Optionally skip "held" targets (`APP_RADAR_HELD_MS`: position frozen, the radar repeating someone it lost) and
  "parked" ones (`APP_RADAR_PARKED_MS`: moved under 30 cm in that time, like a fan). 0 turns a filter off.
- Stay with the person being followed until they are gone for `APP_RADAR_LOCK_LOST_MS`.
- Optionally aim `APP_RADAR_LEAD_MS` ahead of their sideways motion to hide the radar's delay.
- Full eye swing at `APP_RADAR_MAX_ANGLE` degrees off center; `APP_RADAR_MIRROR_X` sets which side is which.

The radar itself has no range or sensitivity setting. It offers two, sent at start and remembered by the radar:
single or multi-target mode (`APP_RADAR_TARGET_MODE`) and a zone filter (`APP_RADAR_ZONE_*`, one rectangle in
radar coordinates, e.g. the sidewalk). Both default to -1 (leave as they are); the log confirms each one.

### Finding center

The sensor in the pumpkin is glued turned about 5 degrees toward the viewer's left, so `APP_RADAR_CENTER_DEG` is 5.0
(measured 2026-10-29: someone standing dead ahead of the eyes at 2 m read +5 degrees). If the sensor is moved or
re-glued: stand still dead ahead of the eyes at 2 m, run `py tools/radar_capture.py --seconds 20`, and set
`APP_RADAR_CENTER_DEG` to the bearing it prints (keep its sign, with `APP_RADAR_MIRROR_X` true).

### Tuning (2026-10-29, upright in the pumpkin, walking at 2 m)

Single-target mode reports about 19 times a second while someone is in view. Tracks to about 45 degrees each
side, with short dropouts beyond that, so `APP_RADAR_MAX_ANGLE` is 45 and `APP_RADAR_LOCK_LOST_MS` 1.5 s bridges
them. A still person jitters about 8 to 10 cm at 2 m, smoothed by `APP_RADAR_SMOOTH_MS` 150. In single mode
the radar repeats a still person's exact position for about 1.5 s, so the held filter is 3 s. Parked filter,
lead and minimum range are off. Blinks: both eyes start within one 33 ms frame (`APP_BLINK_SYNC_LEAD_MS`).

## Two boards (ESP-NOW)

This folder builds the **master** (viewer's left eye). `eye_slave/` builds the **slave** (right eye) from the
same components; only the eye side and ESP-NOW role differ, set in each folder's `CMakeLists.txt`.
Both boards can be plugged in at once (each gets its own COM port).

The master looks around and blinks on its own and broadcasts its gaze (20 Hz) and every blink. The slave holds
center and never blinks by itself; it follows the master and eases back to center if the master goes quiet.
Broadcast and unencrypted by default, so no MAC or key is needed. Tested on two boards.
