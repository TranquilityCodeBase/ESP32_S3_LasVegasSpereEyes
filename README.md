# ESP32_S3_LasVegasSpereEyes

Las Vegas Sphere style eyeballs on the Waveshare ESP32-S3-Touch-LCD-2.8C (round 480x480 ST7701S panel).
A shaded yellow ball with two white cartoon eyes that look around and blink. Later: follow people with an
HLK-LD2450 radar, and keep two boards in step over ESP-NOW.

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
| `main/app_config.h` | Frame rate, radar pins and on/off, ESP-NOW role |
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
main                  Picks style and gaze sources, runs the 30 fps render task
```

Rendering redraws only the area around the eyes each frame (anti-aliased ellipses in an internal-RAM strip),
then page-flips at the panel's frame boundary, so there is no tearing.

- New look: add `styles/style_x.c` with an `eye_style_t`, declare it in `eye_style.h`, pass it to `eyeRendererInit`.
- New blink: add a `blink_effect_t` in `blink/blink_effects.c` and append it to `blinkEffectTable` (append only: the index goes over ESP-NOW).
- New input: implement `gaze_source_t` and add it to `gazeSources` in `app_main.c`. Higher in the list wins.

## Radar (LD2450)

Not wired yet. Plan: radar TX to GPIO19, radar RX to GPIO20, 5 V and GND from the 12-pin header, then set
`APP_RADAR_ENABLED 1`. GPIO43/44 reach the header only with the USB-C port unplugged, so they are not used.
Untested on hardware: if the eyes look the wrong way, set `APP_RADAR_MIRROR_X true`.

## Two boards (ESP-NOW)

Set `APP_SYNC_ROLE` to 1 (leader) on one board and 2 (follower) on the other. Broadcast and unencrypted by
default, so no MAC or key is needed. The follower copies the leader's gaze and blinks, and falls back to its
own idle wander if the leader goes quiet. Untested on hardware.
