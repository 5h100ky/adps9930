# APDS-9930 Illuminance Display

Display the illuminance value from an **APDS-9930** breakout board on a
**ST7789 1.69-inch TFT** (landscape mode) using a **Pro Micro nRF52840**.

The screen backlight brightness is automatically linked to the ambient light
level read by the APDS-9930.

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | SparkFun Pro Micro – nRF52840 (or compatible) |
| Display | ST7789 1.69" TFT, 240 × 280 px |
| Sensor | APDS-9930 Ambient Light & Proximity Sensor |
| Interface | Display: SPI · Sensor: I²C |

---

## Wiring

### ST7789 1.69" Display (SPI)

| ST7789 pin | Pro Micro nRF52840 |
|------------|--------------------|
| GND        | GND                |
| VCC        | 3.3 V              |
| SCL (SCK)  | D15 (SPI SCK)      |
| SDA (MOSI) | D16 (SPI MOSI)     |
| RES (RST)  | D8                 |
| DC         | D9                 |
| CS         | D10                |
| BLK        | D6 (PWM backlight) |

### APDS-9930 (I²C)

| APDS-9930 | Pro Micro nRF52840 |
|-----------|--------------------|
| GND       | GND                |
| VCC       | 3.3 V              |
| SDA       | D2 (I²C SDA)       |
| SCL       | D3 (I²C SCL)       |

---

## Screen layout (landscape 280 × 240)

```
┌──────────────────────────────────────────┐  ← title bar (blue)
│          APDS-9930 Monitor               │
├─────────────────────┬────────────────────┤
│  ILLUMINANCE (lux)  │   MCU STATUS       │
│                     │                    │
│       1234.5        │  UP  00:01:23      │
│         lux         │  CPU  64 MHz       │
│  ████████░░░░░░░░   │  RAM  256 KB       │
│                     │  SENS OK           │
│                     │  BLE  nRF52840     │
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

# Build
pio run

# Build & flash via USB
pio run --target upload

# Open serial monitor
pio device monitor
```

Dependencies are fetched automatically by PlatformIO:

- **Adafruit ST7735 and ST7789 Library** ≥ 1.10.4
- **Adafruit GFX Library** ≥ 1.11.9
- **Adafruit BusIO** ≥ 1.16.1
- **SparkFun APDS-9930 Ambient Light and Proximity Sensor** ≥ 1.0.0

---

## Project structure

```
adps9930/
├── src/
│   └── main.cpp        ← firmware (sensor, display, backlight)
├── platformio.ini      ← PlatformIO build configuration
└── README.md
```
