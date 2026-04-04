# APDS-9930 Illuminance Display

Display the illuminance value from an **APDS-9930** breakout board on a
**ST7789 1.69-inch TFT** (landscape mode) using a **Raspberry Pi Pico (RP2040)**.

The screen backlight brightness is automatically linked to the ambient light
level read by the APDS-9930.

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | Raspberry Pi Pico (RP2040, dual-core ARM Cortex-M0+) |
| Display | ST7789 1.69" TFT, 240 × 280 px |
| Sensor | APDS-9930 Ambient Light & Proximity Sensor |
| Interface | Display: SPI · Sensor: I²C |

---

## Wiring

### ST7789 1.69" Display (SPI0)

| ST7789 pin | Raspberry Pi Pico |
|------------|-------------------|
| GND        | GND               |
| VCC        | 3.3 V             |
| SCL (SCK)  | GP18 (SPI0 SCK)   |
| SDA (MOSI) | GP19 (SPI0 TX)    |
| RES (RST)  | GP21              |
| DC         | GP20              |
| CS         | GP17 (SPI0 CSn)   |
| BLK        | GP22 (PWM backlight) |

### APDS-9930 (I²C0)

| APDS-9930 | Raspberry Pi Pico |
|-----------|-------------------|
| GND       | GND               |
| VCC       | 3.3 V             |
| SDA       | GP4 (I²C0 SDA)    |
| SCL       | GP5 (I²C0 SCL)    |

---

## Screen layout (landscape 280 × 240)

```
┌──────────────────────────────────────────┐  ← title bar (blue)
│          APDS-9930 Monitor               │
├─────────────────────┬────────────────────┤
│  ILLUMINANCE (lux)  │   MCU STATUS       │
│                     │                    │
│       1234.5        │  UP  00:01:23      │
│         lux         │  CPU  125 MHz      │
│  ████████░░░░░░░░   │  RAM  264 KB       │
│                     │  SENS OK           │
│                     │  CORE Dual M0+     │
│                     │  BL    72%         │
├─────────────────────┴────────────────────┤
│  LUX: INDOOR  |  BL:  72%  |  T+42s     │  ← status bar
└──────────────────────────────────────────┘
```

---

## Backlight control

The backlight PWM duty cycle is continuously mapped from the APDS-9930 lux
reading:

| Condition      | Lux       | Backlight       |
|----------------|-----------|-----------------|
| Dark room      | ≤ 5 lux   | ~4 % (minimum)  |
| Bright indoor  | ~500 lux  | ~66 %           |
| Direct sunlight| ≥ 800 lux | 100 % (maximum) |

---

## Build & Flash (PlatformIO)

```bash
# Install PlatformIO CLI if needed
pip install platformio

# Build (generates firmware.uf2)
pio run

# Flash via UF2:
#   1. Hold the BOOTSEL button on the Pico, then plug in USB
#   2. The Pico mounts as a USB drive named RPI-RP2
#   3. Copy the UF2 file to the drive – it reboots automatically
cp .pio/build/pico/firmware.uf2 /media/$USER/RPI-RP2/

# Open serial monitor
pio device monitor
```

Dependencies are fetched automatically by PlatformIO:

- **Adafruit ST7735 and ST7789 Library** ≥ 1.10.4
- **Adafruit GFX Library** ≥ 1.11.9
- **Adafruit BusIO** ≥ 1.16.1
- **APDS9930** (Depau fork, pinned commit)

---

## Project structure

```
adps9930/
├── src/
│   └── main.cpp        ← firmware (sensor, display, backlight)
├── platformio.ini      ← PlatformIO build configuration
└── README.md
```
