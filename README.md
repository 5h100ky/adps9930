# APDS-9930 Illuminance Display

Display the illuminance value from an **APDS-9930** breakout board on an
**SSD1306 0.96-inch OLED** using a **Waveshare RP2040 Zero**.

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | Waveshare RP2040 Zero (dual-core ARM Cortex-M0+) |
| Display | SSD1306 0.96" OLED, 128 × 64 px |
| Sensor | APDS-9930 Ambient Light & Proximity Sensor |
| Interface | Shared I²C0 bus (SDA=GP4, SCL=GP5) |

---

## Wiring

Both the SSD1306 OLED and the APDS-9930 share the same I²C0 bus.

| SSD1306 / APDS-9930 | Waveshare RP2040 Zero |
|---------------------|-----------------------|
| GND                 | GND                   |
| VCC                 | 3.3 V                 |
| SDA                 | GP4 (I²C0 SDA)        |
| SCL                 | GP5 (I²C0 SCL)        |

I²C addresses: SSD1306 → `0x3C` · APDS-9930 → `0x39`

---

## Screen layout (128 × 64 px)

```
┌────────────────────────────────┐
│           APDS-9930            │  ← title (centred)
├────────────────────────────────┤
│                                │
│            1234.5              │  ← lux value (large, centred)
│              lux               │
│  [▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░]   │  ← progress bar (5…800 lux)
├────────────────────────────────┤
│  INDOOR            SENS:OK     │  ← range label · sensor status
└────────────────────────────────┘
```

Range labels: `DARK` · `DIM` · `INDOOR` · `BRIGHT` · `SUNLIGHT`

---

## Build & Flash (PlatformIO)

```bash
# Install PlatformIO CLI if needed
pip install platformio

# Build (generates firmware.uf2)
pio run

# Flash via UF2:
#   1. Hold the BOOT button on the RP2040 Zero, then plug in USB
#   2. The board mounts as a USB drive named RPI-RP2
#   3. Copy the UF2 file to the drive – it reboots automatically
cp .pio/build/waveshare_rp2040_zero/firmware.uf2 /media/$USER/RPI-RP2/

# Open serial monitor
pio device monitor
```

Dependencies are fetched automatically by PlatformIO:

- **Adafruit SSD1306** ≥ 2.5.7
- **Adafruit GFX Library** ≥ 1.11.9
- **Adafruit BusIO** ≥ 1.16.1
- **APDS9930** (Depau fork, pinned commit)

---

## Debug output

Uncomment `#define APP_DEBUG` in `src/main.cpp` to enable verbose Serial
output (115200 baud):

- **setup** – board/pin banner, OLED & sensor init results
- **loop** – lux reading every 500 ms cycle
- **errors** – sensor read failures with cycle counter

---

## Project structure

```
adps9930/
├── src/
│   └── main.cpp        ← firmware (sensor read, OLED UI)
├── platformio.ini      ← PlatformIO build configuration
└── README.md
```
