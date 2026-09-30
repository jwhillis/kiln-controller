# Kiln Controller

ESP32-S3 firmware for a single-zone electric kiln: PID duty-cycle control, a desktop web UI, captive-portal WiFi, OTA, LittleFS documents, and MQTT for Node-RED.

The web stack and look come from [reference/resin_scale](reference/resin_scale). Browse to **http://kiln.local**.

## Wiring (defaults)

| Function | ESP32-S3 GPIO |
|----------|----------------|
| MAX31856/31855 CS | 10 |
| SPI SCK | 12 |
| SPI MISO | 13 |
| SPI MOSI | 11 |
| SSR drive (active high) | 21 |
| Safety relay (optional, 255 = unused) | — |
| Status RGB LED | 38 (DevKitC-1 v1.1; v1.0 often 48) |

Thermocouple: type K. Pick MAX31856 or MAX31855 in **Admin → Hardware**. Change pins there too; reboot after saving.

The SSR should switch the elements (and use a proper snubber / zero-cross SSR). This firmware time-proportions a slow window (default 4 s). It does **not** replace a mechanical safety interlock or a door/over-temp kiln sitter.

## Build & flash

From **PowerShell** (not Git Bash — esptool refuses MSYS):

```powershell
pio run
pio run -t upload
pio device monitor
```

The UI (`web/index.html`) is compiled into the firmware. After the first USB flash, update from **Admin → OTA** using `.pio/build/esp32-s3-devkitc-1/firmware.bin`. OTA is refused while a firing or autotune is running.

## First run

1. Power on. With no saved WiFi, the controller starts a **Kiln-Setup** hotspot (LED blinks blue).
2. Join it. The captive portal opens WiFi setup, or skip to `http://192.168.4.1`.
3. After it joins your network the LED is green. Open **http://kiln.local**.
4. **Admin** password defaults to `admin`. Set PID gains (or run autotune on an empty kiln), element wattage, and thermocouple chip.

Hold **BOOT** 3 s at power-up to clear saved WiFi.

## Firing

- **Profiles** tab: drag the curve or edit the table. Quartz / cristobalite bands slow the ramp through those temperatures. Enable fire-down to anneal or cool thick work under control.
- **Firing** tab: pick a profile, optional delay, Start / Pause / Skip / Stop.
- Example profiles are seeded on first boot: Cone 6 Glaze, Glass Anneal, Slow Bisque.

Temperatures are stored in Celsius. The display can be °F or °C.

## MQTT (Mosquitto / Node-RED)

Enable in Admin. Default base topic `kiln/`.

| Topic | Direction |
|-------|-----------|
| `kiln/status` | JSON: temp, setpoint, output, state, segment, eta, kwh |
| `kiln/alarm` | last fault string |
| `kiln/availability` | `online` / LWT `offline` |
| `kiln/cmd/start` | profile name or `{"profile":"…"}` (if remote start is enabled) |
| `kiln/cmd/stop` | stop |
| `kiln/cmd/pause` | pause |

An example Node-RED import is in [docs/nodered-flow.json](docs/nodered-flow.json).

## Safety trips

Over-temperature, thermocouple fault, runaway (rising with output off), stall (100% with no rise), max duration. Any trip forces the SSR off and drops the safety relay if one is wired.
