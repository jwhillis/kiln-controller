# Resin Scale

WiFi scale for measuring two-part epoxy and silicone, plus a small-parts counter.
It runs on an ESP32-S3 DevKitC (8 MB flash) with an HX711 and a load cell.
The UI is served at **http://scale.local**.

## Wiring

| HX711 | ESP32-S3 |
|-------|----------|
| VCC   | 3V3      |
| GND   | GND      |
| DT / DOUT | GPIO 4 |
| SCK   | GPIO 5   |

The load cell goes to the HX711's E+/E-/A+/A- (usually red/black/white/green).
If the weight reads negative, don't rewire it. Calibration handles either orientation.

Most HX711 boards run at **10 samples/s** (RATE pin tied to GND). If your board
has a RATE jumper, setting it to 80 SPS makes the pour display track noticeably faster.

To use different pins, change `PIN_HX_DOUT` / `PIN_HX_SCK` at the top of `src/main.cpp`.

## Build & flash

Run these from **PowerShell**, not Git Bash (esptool refuses to run under MSYS):

```powershell
pio run                  # build
pio run -t upload        # flash over USB
pio device monitor       # serial log, 115200 baud
```

The web UI (`web/index.html`) is compiled into the firmware, so there is no `uploadfs` step.
After the first USB flash, update over WiFi from **Admin → OTA Firmware Update** using
`.pio/build/esp32-s3-devkitc-1/firmware.bin`.

## First run

1. Power on. With no saved WiFi, the scale starts a **Scale-Setup** hotspot and the LED blinks blue.
2. Join it from your phone. The captive portal opens WiFi setup, where you pick your network and enter the password.
   You can also skip setup and use the scale directly from the hotspot at `http://192.168.4.1`.
3. The scale restarts, joins your WiFi, and the LED turns solid green. Browse to **http://scale.local**.
4. Open **Admin & Calibration**. Set zero with the platform empty, then place a known weight,
   enter its mass, and tap **Calibrate**. The factor is saved and survives reboots.

If saved WiFi is unreachable at boot (for example, the router is still starting after a power cut),
the scale falls back to the hotspot. While no one is connected to the hotspot, it retries your network every minute.

Hold **BOOT** for 3 s while powering on to clear saved WiFi. A short press of **BOOT** while running tares the scale.

## Mixing

1. Enter the ratio (A : B) and choose whether it's **by weight** or **by volume**.
   Volume ratios need each part's specific gravity from the product data sheet, and the scale converts to weight.
2. Enter how much mixed resin you need, plus an optional waste allowance.
   This is grams (or oz) in weight mode and mL in volume mode.
3. **Save** it as a product so next time it's one tap. Products are stored on the scale, so every phone sees them.
4. Put the empty cup on the scale and tap **Tare cup & pour Part A**. The Part A beaker fills as you pour.
   It turns green inside the tolerance band and red if you go over.
5. Tap **Lock Part A → Part B**. The Part B target is recalculated from the Part A you
   *actually* poured, so an overpour of A doesn't throw off the ratio.
6. Pour B and tap **Finish** to see the actual ratio achieved.

## Counting

Tare the empty container, add a known number of parts (10 by default), and tap **Sample**.
Then pour in the rest. **Refine** re-averages the piece weight over the larger count,
which is more accurate. You can save piece weights to the Parts Library and set an optional target count.

## Status LED (on-board RGB)

| LED | Meaning |
|-----|---------|
| Blinking blue | Hotspot / captive-portal mode |
| Solid green | Connected to WiFi |
| Blinking red | HX711 not responding (check wiring) |

The LED is on GPIO 38 (DevKitC-1 v1.1). For a v1.0 board, change `PIN_STATUS_LED` in `src/main.cpp` to 48.

## HTTP API

| Route | Method | Purpose |
|-------|--------|---------|
| `/api/weight` | GET | `{g, tare_sum, stable, taring, ok, seq, ap}` |
| `/api/tare` | POST | Tare (averages 10 readings, non-blocking) |
| `/api/cal` | GET | Calibration state and live raw reading |
| `/api/cal/zero`, `/api/cal/span` (`grams=`), `/api/cal/factor` (`factor=`), `/api/cal/boot-tare` (`on=`) | POST | Calibration |
| `/api/store?key=presets\|parts` | GET / POST (`data=`) | Product and parts libraries (JSON on LittleFS) |
| `/api/info` | GET | Firmware, network and memory info |
| `/api/ota` | POST | Firmware upload |
