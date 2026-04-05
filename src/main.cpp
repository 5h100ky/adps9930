/**
 * APDS-9930 Illuminance Display on SSD1306 0.96" OLED
 *
 * Hardware
 * ─────────────────────────────────────────────────────────
 * MCU    : Waveshare RP2040 Zero
 * Sensor : APDS-9930 Ambient Light & Proximity Sensor (I2C)
 * Display: SSD1306 0.96" OLED, 128×64 px (I2C)
 *
 * Wiring  (shared I2C0 bus)
 * ─────────────────────────────────────────────────────────
 *  SSD1306 / APDS-9930 │ RP2040 Zero
 *  ────────────────────┼──────────────────────────
 *  GND                 │ GND
 *  VCC                 │ 3.3 V
 *  SDA                 │ GP4  (I2C0 SDA)
 *  SCL                 │ GP5  (I2C0 SCL)
 *
 *  SSD1306  I2C address : 0x3C
 *  APDS-9930 I2C address: 0x39
 *
 * Features
 * ─────────────────────────────────────────────────────────
 *  • Lux value (large, centred)  – numeric reading
 *  • Progress bar                – maps 5…800 lux
 *  • Status line                 – range label + sensor state
 *  • Update rate                 – 500 ms
 *
 * Debug
 * ─────────────────────────────────────────────────────────
 *  Define DEBUG (below) to enable verbose Serial output:
 *    setup  – board/pin banner, init results for OLED & sensor
 *    loop   – lux reading every UPDATE_MS cycle
 *    errors – sensor read failures with cycle counter
 */

// ── Debug switch ──────────────────────────────────────────────────────────────
// Uncomment to enable verbose Serial output (setup banner + per-cycle readings).
// #define DEBUG

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
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
#ifdef DEBUG
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
APDS9930 apds;

// ── State ─────────────────────────────────────────────────────────────────────
static float    g_lux        = 0.0f;
static bool     g_sensorOk   = false;
static uint32_t g_lastUpdate = 0;
static uint32_t g_startMs    = 0;
#ifdef DEBUG
static uint32_t g_loopCount  = 0;   // update-cycle counter (debug only)
#endif

// ── Prototype ─────────────────────────────────────────────────────────────────
static void drawUI();

// =============================================================================
void setup()
{
    Serial.begin(115200);
#ifdef DEBUG
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
    Wire.begin();   // GP4=SDA, GP5=SCL (default I2C0 on RP2040 Zero)
    DBGLN("OK (I2C0: SDA=GP4, SCL=GP5)");

    // ── OLED ──
    DBG("[INIT] SSD1306 OLED ... ");
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
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
        apds.enableLightSensor(false);   // polling mode
        DBGLN("OK (light sensor enabled, polling mode)");
        delay(200);
    } else {
        DBGLN("FAIL – check wiring (SDA=GP4, SCL=GP5) and I2C address (0x39)");
    }

    g_startMs = millis();
    DBGLN("[INIT] Setup complete");
    DBGLN("----------------------------------------");
}

// =============================================================================
void loop()
{
    uint32_t now = millis();
    if (now - g_lastUpdate < UPDATE_MS) return;
    g_lastUpdate = now;
#ifdef DEBUG
    g_loopCount++;
#endif

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
// Full-screen UI (buffered – one display.display() call per cycle)
//
// Layout  (128×64 px, text size 1 = 6×8 px per char)
// ─────────────────────────────────────────────────────
//  y= 0  "APDS-9930"  title  (size 1, centred)
//  y= 9  ─ separator line ─
//  y=12  lux number          (size 2 = 12×16 px, centred)
//  y=30  "lux"               (size 1, centred)
//  y=40  [▓▓▓▓▓░░░░░]       progress bar  (h=6)
//  y=48  ─ separator line ─
//  y=52  range label (left)  |  sensor status (right)
// =============================================================================
static void drawUI()
{
    display.clearDisplay();

    int16_t  tx, ty;
    uint16_t tw, th;

    // ── Title (y=0, size=1, h=8) ──────────────────────────────────────────────
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.getTextBounds("APDS-9930", 0, 0, &tx, &ty, &tw, &th);
    display.setCursor((SCREEN_W - (int16_t)tw) / 2, 0);
    display.print("APDS-9930");

    // ── Separator (y=9) ───────────────────────────────────────────────────────
    display.drawFastHLine(0, 9, SCREEN_W, SSD1306_WHITE);

    // ── Lux value (y=12, size=2, h=16) ───────────────────────────────────────
    char buf[16];
    if (g_lux < 10000.0f) {
        snprintf(buf, sizeof(buf), "%.1f", g_lux);
    } else {
        snprintf(buf, sizeof(buf), "%.0f", g_lux);
    }
    const char *num = buf;
    while (*num == ' ') num++;   // trim leading spaces (snprintf safety)

    display.setTextSize(2);
    display.getTextBounds(num, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor((SCREEN_W - (int16_t)tw) / 2, 12);
    display.print(num);

    // ── "lux" label (y=30, size=1, h=8) ──────────────────────────────────────
    display.setTextSize(1);
    display.getTextBounds("lux", 0, 0, &tx, &ty, &tw, &th);
    display.setCursor((SCREEN_W - (int16_t)tw) / 2, 30);
    display.print("lux");

    // ── Progress bar (y=40, h=6) ──────────────────────────────────────────────
    const int16_t bx = 2, by = 40, bw = SCREEN_W - 4, bh = 6;
    display.drawRect(bx, by, bw, bh, SSD1306_WHITE);
    float ratio = (g_lux - LUX_DIM) / (LUX_BRIGHT - LUX_DIM);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    int16_t fill = (int16_t)(ratio * (float)(bw - 2));
    if (fill > 0) {
        display.fillRect(bx + 1, by + 1, fill, bh - 2, SSD1306_WHITE);
    }

    // ── Separator (y=48) ──────────────────────────────────────────────────────
    display.drawFastHLine(0, 48, SCREEN_W, SSD1306_WHITE);

    // ── Status line (y=52, size=1, h=8) ──────────────────────────────────────
    // Left: lux range label
    const char *rangeStr;
    if      (g_lux <  LUX_RANGE_DIM)    rangeStr = "DARK";
    else if (g_lux <  LUX_RANGE_INDOOR) rangeStr = "DIM";
    else if (g_lux <  LUX_RANGE_BRIGHT) rangeStr = "INDOOR";
    else if (g_lux <  LUX_RANGE_SUNLIT) rangeStr = "BRIGHT";
    else                                 rangeStr = "SUNLIGHT";

    display.setTextSize(1);
    display.setCursor(2, 52);
    display.print(rangeStr);

    // Right: sensor status
    const char *sensStr = g_sensorOk ? "SENS:OK" : "SENS:ERR";
    display.getTextBounds(sensStr, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor(SCREEN_W - (int16_t)tw - 2, 52);
    display.print(sensStr);

    // ── Push frame to OLED ────────────────────────────────────────────────────
    display.display();
}
