/**
 * APDS-9930 Illuminance + DS3231 Clock Display on SSD1306 0.96" OLED
 *
 * Hardware
 * ─────────────────────────────────────────────────────────
 * MCU    : Waveshare RP2040 Zero
 * Sensor : APDS-9930 Ambient Light & Proximity Sensor (I2C)
 * RTC    : DS3231 Real-Time Clock (I2C)
 * Display: SSD1306 0.96" OLED, 128×64 px (I2C)
 *
 * Wiring  (shared I2C0 bus)
 * ─────────────────────────────────────────────────────────
 *  SSD1306 / APDS-9930 / DS3231 │ RP2040 Zero
 *  ──────────────────────────────┼──────────────────────────
 *  GND                           │ GND
 *  VCC                           │ 3.3 V
 *  SDA                           │ GP4  (I2C0 SDA)
 *  SCL                           │ GP5  (I2C0 SCL)
 *
 *  SSD1306   I2C address: 0x3C
 *  APDS-9930 I2C address: 0x39
 *  DS3231    I2C address: 0x68
 *
 * Display Layout  (128×64 px)
 * ─────────────────────────────────────────────────────────
 *  y= 0  HH:MM:SS          ← time widget   (size 2, 16 px)
 *  y=17  DDD YYYY-MM-DD    ← date widget   (size 1,  8 px)
 *  y=26  ─ separator ─
 *  y=28  Lux: 9999.9       ← lux reading   (size 1,  8 px)
 *  y=37  [▓▓▓░░░░░░░]      ← progress bar  (h=6)
 *  y=44  ─ separator ─
 *  y=47  INDOOR  RTC:OK    ← status line   (size 1,  8 px)
 *
 * Features
 * ─────────────────────────────────────────────────────────
 *  • Time / Date widget  – reads DS3231 via I2C
 *  • Lux reading         – APDS-9930 ambient light
 *  • Progress bar        – maps 5…800 lux
 *  • Status line         – lux range label + RTC state
 *  • Update rate         – 500 ms
 *  • RTC auto-sync       – resets to compile timestamp on first flash
 *                          or whenever RTC time is behind compile time
 *  • Serial time sync    – send  T<YYYY-MM-DD HH:MM:SS>  over serial
 *                          (115200 baud) to sync RTC to PC time exactly,
 *                          e.g.  T2026-04-09 22:57:41
 *
 * Debug
 * ─────────────────────────────────────────────────────────
 *  Define APP_DEBUG (below) to enable verbose Serial output:
 *    setup  – board/pin banner, init results for OLED, sensor & RTC
 *    loop   – lux reading + timestamp every UPDATE_MS cycle
 *    errors – sensor/RTC read failures with cycle counter
 */

// ── Debug switch ──────────────────────────────────────────────────────────────
// Uncomment to enable verbose Serial output (setup banner + per-cycle readings).
// Note: do NOT use the name DEBUG here – APDS9930.h already defines #define DEBUG 0.
// #define APP_DEBUG

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <APDS9930.h>

// ── Display ───────────────────────────────────────────────────────────────────
#define SCREEN_W   128
#define SCREEN_H    64
#define OLED_ADDR  0x3C
#define OLED_RESET  (-1)   // no dedicated reset pin

// ── Illuminance → progress-bar mapping ───────────────────────────────────────
static const float    LUX_DIM    =   5.0f;   // lux below this → empty bar
static const float    LUX_BRIGHT = 800.0f;   // lux above this → full bar

// ── Lux range labels ─────────────────────────────────────────────────────────
static const float    LUX_RANGE_DIM     =   10.0f;
static const float    LUX_RANGE_INDOOR  =  100.0f;
static const float    LUX_RANGE_BRIGHT  =  500.0f;
static const float    LUX_RANGE_SUNLIT  = 2000.0f;

// ── Update interval ───────────────────────────────────────────────────────────
static const uint32_t UPDATE_MS = 500;

// ── Debug helpers ─────────────────────────────────────────────────────────────
#ifdef APP_DEBUG
  #define DBG(...)   Serial.print(__VA_ARGS__)
  #define DBGLN(...) Serial.println(__VA_ARGS__)
  #define DBGF(fmt, ...) do { char _dbg[80]; snprintf(_dbg, sizeof(_dbg), fmt, ##__VA_ARGS__); Serial.print(_dbg); } while(0)
#else
  #define DBG(...)   ((void)0)
  #define DBGLN(...) ((void)0)
  #define DBGF(...)  ((void)0)
#endif

// ── Global objects ────────────────────────────────────────────────────────────
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, OLED_RESET);
APDS9930        apds;
RTC_DS3231      rtc;

// ── State ─────────────────────────────────────────────────────────────────────
static float    g_lux        = 0.0f;
static bool     g_sensorOk   = false;
static bool     g_rtcOk      = false;
static DateTime g_now;
static uint32_t g_lastUpdate = 0;
#ifdef APP_DEBUG
static uint32_t g_loopCount  = 0;   // update-cycle counter (debug only)
#endif

// ── Prototype ─────────────────────────────────────────────────────────────────
static void drawUI();
static void handleSerialSync();

// =============================================================================
void setup()
{
    Serial.begin(115200);
#ifdef APP_DEBUG
    // Wait up to 3 s for a serial terminal so the banner is visible.
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0 < 3000)) { /* spin */ }
    DBGLN("\n========================================");
    DBGLN(" APDS-9930 Monitor  –  RP2040 Zero");
    DBGLN(" Board  : Waveshare RP2040 Zero (dual-core Cortex-M0+)");
    DBGF   (" F_CPU  : %lu MHz\n", (unsigned long)(SystemCoreClock / 1000000UL));
    DBGLN(" Display: SSD1306 0.96\" OLED (I2C0, 0x3C)");
    DBGLN("   SDA=GP4  SCL=GP5");
    DBGLN(" Sensor : APDS-9930 (I2C0, 0x39)");
    DBGLN("   SDA=GP4  SCL=GP5");
    DBGLN("========================================");
#endif

    // ── I2C bus ──
    DBG("[INIT] I2C Wire    ... ");
    Wire.setSDA(4);     // GP4=SDA (I2C0 on RP2040 Zero)
    Wire.setSCL(5);     // GP5=SCL (I2C0 on RP2040 Zero)
    Wire.begin();
    DBGLN("OK (I2C0: SDA=GP4, SCL=GP5)");

    // ── OLED ──
    DBG("[INIT] SSD1306 OLED ... ");
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, /* reset= */true, /* periphBegin= */false)) {
        Serial.println("[INIT] SSD1306 FAIL – check wiring (SDA=GP4, SCL=GP5) and address (0x3C)");
        for (;;) delay(1000);   // halt – nothing else can be shown
    }
    DBGLN("OK (128x64, I2C 0x3C)");

    // Boot splash
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(22, 28);
    display.print("Initializing...");
    display.display();

    // ── Sensor ──
    DBG("[INIT] APDS-9930   ... ");
    g_sensorOk = apds.init();
    if (g_sensorOk) {
        g_sensorOk = apds.enableLightSensor(false);   // polling mode
        if (g_sensorOk) {
            DBGLN("OK (light sensor enabled, polling mode)");
        } else {
            DBGLN("FAIL – enableLightSensor() returned false");
        }
        delay(200);
    } else {
        DBGLN("FAIL – check wiring (SDA=GP4, SCL=GP5) and I2C address (0x39)");
    }

    DBGLN("[INIT] Setup complete");
    DBGLN("----------------------------------------");

    // ── RTC (DS3231) ──
    DBG("[INIT] DS3231 RTC   ... ");
    g_rtcOk = rtc.begin(&Wire);
    if (g_rtcOk) {
        DateTime compiled(F(__DATE__), F(__TIME__));
        if (rtc.lostPower() || rtc.now() < compiled) {
            // First run, battery died, or RTC is behind compile timestamp –
            // reset to the time this firmware was built.
            rtc.adjust(compiled);
            DBGLN("OK (time set to compile timestamp)");
        } else {
            DBGLN("OK");
        }
        g_now = rtc.now();
    } else {
        DBGLN("FAIL – check wiring (SDA=GP4, SCL=GP5) and I2C address (0x68)");
    }

    DBGLN("[INIT] All initialisation complete");
    DBGLN("----------------------------------------");
}

// =============================================================================
void loop()
{
    uint32_t now = millis();
    if (now - g_lastUpdate < UPDATE_MS) return;
    g_lastUpdate = now;
#ifdef APP_DEBUG
    g_loopCount++;
#endif

    // ── Handle serial time-sync command ──
    handleSerialSync();

    // ── Read RTC ──
    if (g_rtcOk) {
        g_now = rtc.now();
        DBGF("[LOOP #%lu] time=%02u:%02u:%02u  date=%04u-%02u-%02u\n",
             (unsigned long)g_loopCount,
             g_now.hour(), g_now.minute(), g_now.second(),
             g_now.year(), g_now.month(),  g_now.day());
    }

    // ── Read sensor ──
    if (g_sensorOk) {
        float newLux = 0.0f;
        if (apds.readAmbientLightLux(newLux)) {
            g_lux = newLux;
            DBGF("[LOOP #%lu] lux=%.1f\n", (unsigned long)g_loopCount, g_lux);
        } else {
            DBGF("[LOOP #%lu] APDS-9930 readAmbientLightLux() failed\n",
                 (unsigned long)g_loopCount);
        }
    } else {
        DBGF("[LOOP #%lu] sensor not initialised – skipping read\n",
             (unsigned long)g_loopCount);
    }

    // ── Refresh display ──
    drawUI();
}

// =============================================================================
// Serial time-sync
//
// Send a line in the format:   T<YYYY-MM-DD HH:MM:SS>
// Example (Python):
//   import serial, datetime
//   s = serial.Serial('/dev/ttyACM0', 115200)
//   s.write(('T' + datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S') + '\n').encode())
// Example (manual terminal): type  T2026-04-09 22:57:41  then press Enter
// =============================================================================
static char  s_serialBuf[32];
static uint8_t s_serialLen = 0;

static void handleSerialSync()
{
    // Process at most 32 characters per call to avoid blocking the main loop.
    uint8_t processed = 0;
    while (processed < 32 && Serial.available()) {
        char c = (char)Serial.read();
        processed++;
        if (c == '\n' || c == '\r') {
            if (s_serialLen > 0) {
                s_serialBuf[s_serialLen] = '\0';
                // Expect: T<YYYY-MM-DD HH:MM:SS>  (20 chars total)
                // Format: T2026-04-09 22:57:41
                //          0    5  8  1  4  7
                //                   1 1  1  1
                if (s_serialBuf[0] == 'T' && s_serialLen >= 20 &&
                    s_serialBuf[5]  == '-' && s_serialBuf[8]  == '-' &&
                    s_serialBuf[11] == ' ' && s_serialBuf[14] == ':' &&
                    s_serialBuf[17] == ':') {
                    const char *p = s_serialBuf + 1;
                    uint16_t yr  = (uint16_t)atoi(p);           // YYYY
                    uint8_t  mon = (uint8_t)atoi(p + 5);        // MM
                    uint8_t  day = (uint8_t)atoi(p + 8);        // DD
                    uint8_t  hr  = (uint8_t)atoi(p + 11);       // HH
                    uint8_t  mn  = (uint8_t)atoi(p + 14);       // MM
                    uint8_t  sec = (uint8_t)atoi(p + 17);       // SS

                    // Days per month (non-leap / leap for Feb)
                    static const uint8_t daysInMonth[] =
                        { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
                    uint8_t maxDay = (mon == 2 && (yr % 4 == 0 &&
                                     (yr % 100 != 0 || yr % 400 == 0)))
                                     ? 29 : (mon >= 1 && mon <= 12
                                            ? daysInMonth[mon] : 0);

                    if (yr >= 2000 && mon >= 1 && mon <= 12 &&
                        day >= 1   && day <= maxDay          &&
                        hr  <= 23  && mn  <= 59  && sec <= 59) {
                        DateTime dt(yr, mon, day, hr, mn, sec);
                        if (g_rtcOk) {
                            rtc.adjust(dt);
                            g_now = rtc.now();
                            Serial.println("[SYNC] RTC updated OK");
                        } else {
                            Serial.println("[SYNC] RTC not available");
                        }
                    } else {
                        Serial.println("[SYNC] Bad date/time values");
                    }
                } else {
                    Serial.println("[SYNC] Usage: T<YYYY-MM-DD HH:MM:SS>");
                }
                s_serialLen = 0;
            }
        } else {
            if (s_serialLen < sizeof(s_serialBuf) - 1) {
                s_serialBuf[s_serialLen++] = c;
            }
        }
    }
}

// =============================================================================
// Day-of-week abbreviations (0=Sunday … 6=Saturday)
// =============================================================================
static const char* const DAY_NAMES[] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

// =============================================================================
// Full-screen UI (buffered – one display.display() call per cycle)
//
// Layout  (128×64 px, text size 1 = 6×8 px, text size 2 = 12×16 px)
// ─────────────────────────────────────────────────────────────────────
//  y= 0  HH:MM:SS        (size 2, h=16, centred)    ← time widget
//  y=17  DDD YYYY-MM-DD  (size 1, h= 8, centred)    ← date widget
//  y=26  ─ separator line ─
//  y=28  Lux: 9999.9     (size 1, h= 8, centred)    ← lux reading
//  y=37  [▓▓▓░░░░░░░]    (h=6)                      ← progress bar
//  y=44  ─ separator line ─
//  y=47  INDOOR   RTC:OK (size 1, h= 8)              ← status line
// =============================================================================
static void drawUI()
{
    display.clearDisplay();

    int16_t  tx, ty;
    uint16_t tw, th;

    // ── Time widget (y=0, size=2, h=16) ──────────────────────────────────────
    char timeBuf[9];   // "HH:MM:SS\0"
    if (g_rtcOk) {
        snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u",
                 g_now.hour(), g_now.minute(), g_now.second());
    } else {
        snprintf(timeBuf, sizeof(timeBuf), "--:--:--");
    }
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.getTextBounds(timeBuf, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor((SCREEN_W - (int16_t)tw) / 2, 0);
    display.print(timeBuf);

    // ── Date widget (y=17, size=1, h=8) ──────────────────────────────────────
    char dateBuf[16];  // "WED 2026-04-09\0"
    if (g_rtcOk) {
        snprintf(dateBuf, sizeof(dateBuf), "%s %04u-%02u-%02u",
                 DAY_NAMES[g_now.dayOfTheWeek()],
                 g_now.year(), g_now.month(), g_now.day());
    } else {
        snprintf(dateBuf, sizeof(dateBuf), "--- ----------");
    }
    display.setTextSize(1);
    display.getTextBounds(dateBuf, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor((SCREEN_W - (int16_t)tw) / 2, 17);
    display.print(dateBuf);

    // ── Separator (y=26) ──────────────────────────────────────────────────────
    display.drawFastHLine(0, 26, SCREEN_W, SSD1306_WHITE);

    // ── Lux reading (y=28, size=1, h=8) ──────────────────────────────────────
    char luxBuf[20];
    if (g_lux < 10000.0f) {
        snprintf(luxBuf, sizeof(luxBuf), "Lux: %.1f", g_lux);
    } else {
        snprintf(luxBuf, sizeof(luxBuf), "Lux: %.0f", g_lux);
    }
    display.setTextSize(1);
    display.getTextBounds(luxBuf, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor((SCREEN_W - (int16_t)tw) / 2, 28);
    display.print(luxBuf);

    // ── Progress bar (y=37, h=6) ──────────────────────────────────────────────
    const int16_t bx = 2, by = 37, bw = SCREEN_W - 4, bh = 6;
    display.drawRect(bx, by, bw, bh, SSD1306_WHITE);
    float ratio = (g_lux - LUX_DIM) / (LUX_BRIGHT - LUX_DIM);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    int16_t fill = (int16_t)(ratio * (float)(bw - 2));
    if (fill > 0) {
        display.fillRect(bx + 1, by + 1, fill, bh - 2, SSD1306_WHITE);
    }

    // ── Separator (y=44) ──────────────────────────────────────────────────────
    display.drawFastHLine(0, 44, SCREEN_W, SSD1306_WHITE);

    // ── Status line (y=47, size=1, h=8) ──────────────────────────────────────
    // Left: lux range label
    const char *rangeStr;
    if      (g_lux <  LUX_RANGE_DIM)    rangeStr = "DARK";
    else if (g_lux <  LUX_RANGE_INDOOR) rangeStr = "DIM";
    else if (g_lux <  LUX_RANGE_BRIGHT) rangeStr = "INDOOR";
    else if (g_lux <  LUX_RANGE_SUNLIT) rangeStr = "BRIGHT";
    else                                 rangeStr = "SUNLIGHT";

    display.setTextSize(1);
    display.setCursor(2, 47);
    display.print(rangeStr);

    // Right: RTC status
    const char *rtcStr = g_rtcOk ? "RTC:OK" : "RTC:ERR";
    display.getTextBounds(rtcStr, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor(SCREEN_W - (int16_t)tw - 2, 47);
    display.print(rtcStr);

    // ── Push frame to OLED ────────────────────────────────────────────────────
    display.display();
}
