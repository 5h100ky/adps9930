/**
 * APDS-9930 Illuminance Display on ST7789 1.69" (Landscape)
 *
 * Hardware
 * ─────────────────────────────────────────────────────────
 * MCU    : Raspberry Pi Pico (RP2040)
 * Sensor : APDS-9930 Ambient Light & Proximity Sensor (I2C)
 * Display: ST7789 1.69" TFT, 240×280 px  →  landscape 280×240
 *
 * Wiring
 * ─────────────────────────────────────────────────────────
 *  ST7789 pin  │ Raspberry Pi Pico (RP2040)
 *  ────────────┼──────────────────────────
 *  GND         │ GND
 *  VCC         │ 3.3 V
 *  SCL  (SCK)  │ GP18  (SPI0 SCK)
 *  SDA  (MOSI) │ GP19  (SPI0 TX)
 *  RES  (RST)  │ GP21
 *  DC          │ GP20
 *  CS          │ GP17  (SPI0 CSn)
 *  BLK  (PWM)  │ GP22  ← backlight brightness (PWM)
 *
 *  APDS-9930   │ Raspberry Pi Pico (RP2040)
 *  ────────────┼──────────────────────────
 *  GND         │ GND
 *  VCC         │ 3.3 V
 *  SDA         │ GP4   (I2C0 SDA)
 *  SCL         │ GP5   (I2C0 SCL)
 *
 * Features
 * ─────────────────────────────────────────────────────────
 *  • Illuminance widget  – numeric lux value + progress bar
 *  • MCU status widget   – uptime, CPU freq, RAM, sensor/BLE
 *  • Backlight control   – PWM brightness tracks lux level
 *  • Update rate         – 500 ms
 *
 * Debug
 * ─────────────────────────────────────────────────────────
 *  Define DEBUG (below) to enable verbose Serial output:
 *    setup  – board/pin banner, init results for TFT & sensor
 *    loop   – lux reading + backlight % every UPDATE_MS cycle
 *    errors – sensor read failures with cycle counter
 */

// ── Debug switch ──────────────────────────────────────────────────────────────
// Uncomment to enable verbose Serial output (setup banner + per-cycle readings).
// #define DEBUG

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <APDS9930.h>

// ── Pin definitions ───────────────────────────────────────────────────────────
#define TFT_CS   17   // GP17 (SPI0 CSn)
#define TFT_DC   20   // GP20
#define TFT_RST  21   // GP21
#define TFT_BL   22   // GP22 – PWM backlight

// ── Display geometry (landscape) ─────────────────────────────────────────────
static const uint16_t SCREEN_W = 280;
static const uint16_t SCREEN_H = 240;

// ── Illuminance → brightness mapping ─────────────────────────────────────────
static const uint8_t  BL_MIN      = 10;    // PWM floor  (~4 %)
static const uint8_t  BL_MAX      = 255;   // PWM ceiling (100 %)
static const float    LUX_DIM     = 5.0f;  // lux below this → BL_MIN
static const float    LUX_BRIGHT  = 800.0f;// lux above this → BL_MAX

// ── Update interval ───────────────────────────────────────────────────────────
static const uint32_t UPDATE_MS = 500;

// ── Layout constants ──────────────────────────────────────────────────────────
static const uint16_t TITLE_H    = 28;
static const uint16_t STATUS_Y   = 154;
static const uint16_t STATUS_H   = 26;
static const uint16_t PANEL_Y    = 32;
static const uint16_t PANEL_H    = 120;   // PANEL_Y..STATUS_Y
static const uint16_t LUX_PAN_X  = 3;
static const uint16_t LUX_PAN_W  = 136;
static const uint16_t MCU_PAN_X  = 143;
static const uint16_t MCU_PAN_W  = 134;

// ── Colour helpers ────────────────────────────────────────────────────────────
static inline uint16_t C(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r & 0xF8) << 8)
         | ((uint16_t)(g & 0xFC) << 3)
         |  (uint16_t)(b >> 3);
}

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
Adafruit_ST7789  tft(TFT_CS, TFT_DC, TFT_RST);
APDS9930 apds;

// ── State ─────────────────────────────────────────────────────────────────────
static float    g_lux        = 0.0f;
static bool     g_sensorOk   = false;
static uint32_t g_lastUpdate = 0;
static uint32_t g_startMs    = 0;
static uint8_t  g_brightness = BL_MAX;
#ifdef DEBUG
static uint32_t g_loopCount  = 0;   // update-cycle counter (debug only)
#endif

// ── Prototypes ────────────────────────────────────────────────────────────────
static void drawStaticUI();
static void updateLuxWidget();
static void updateMcuWidget();
static void updateStatusBar();
static void applyBacklight(float luxVal);

// =============================================================================
void setup()
{
    Serial.begin(115200);
#ifdef DEBUG
    // Wait up to 3 s for a serial terminal so the banner is visible.
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0 < 3000)) { /* spin */ }
    DBGLN("\n========================================");
    DBGLN(" APDS-9930 Monitor  –  Raspberry Pi Pico");
    DBGLN(" Board  : RP2040 (dual-core Cortex-M0+)");
    DBGF   (" F_CPU  : %lu MHz\n", (unsigned long)(SystemCoreClock / 1000000UL));
    DBGLN(" Display: ST7789 1.69\" (SPI0)");
    DBGLN("   CS=GP17  DC=GP20  RST=GP21  BL=GP22");
    DBGLN(" Sensor : APDS-9930 (I2C0)");
    DBGLN("   SDA=GP4  SCL=GP5");
    DBGLN("========================================");
#endif

    // ── Backlight on immediately ──
    DBG("[INIT] Backlight ... ");
    pinMode(TFT_BL, OUTPUT);
    analogWrite(TFT_BL, BL_MAX);
    DBGLN("OK (PWM GP22, duty=255)");

    // ── Display: native portrait 240×280, then rotate to landscape 280×240 ──
    DBG("[INIT] ST7789 TFT  ... ");
    tft.init(240, 280);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    DBGLN("OK (landscape 280x240)");

    // Boot splash
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setCursor(52, 108);
    tft.print("Initializing...");

    // ── Sensor init ──
    DBG("[INIT] I2C Wire    ... ");
    Wire.begin();   // GP4=SDA, GP5=SCL (default I2C0 on Pico)
    DBGLN("OK (I2C0: SDA=GP4, SCL=GP5)");

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
    DBGLN("[INIT] Drawing static UI ...");
    drawStaticUI();
    DBGF("[INIT] Setup complete in %lu ms\n", (unsigned long)(millis() - g_startMs));
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
#ifdef DEBUG
            DBGF("[LOOP #%lu] lux=%.1f  BL=%u/255 (%u%%)\n",
                 (unsigned long)g_loopCount, g_lux, g_brightness,
                 (unsigned)(((uint16_t)(g_brightness - BL_MIN) * 100u) / (BL_MAX - BL_MIN)));
#endif
        } else {
#ifdef DEBUG
            DBGF("[LOOP #%lu] APDS-9930 readAmbientLightLux() failed\n",
                 (unsigned long)g_loopCount);
#endif
        }
    } else {
#ifdef DEBUG
        DBGF("[LOOP #%lu] sensor not initialised – skipping read\n",
             (unsigned long)g_loopCount);
#endif
    }

    // ── Apply brightness ──
    applyBacklight(g_lux);

    // ── Refresh widgets ──
    updateLuxWidget();
    updateMcuWidget();
    updateStatusBar();
}

// =============================================================================
// Static chrome – drawn once at startup
// =============================================================================
static void drawStaticUI()
{
    tft.fillScreen(ST77XX_BLACK);

    // Title bar
    tft.fillRect(0, 0, SCREEN_W, TITLE_H, C(0, 80, 160));
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setCursor(6, 6);
    tft.print("APDS-9930 Monitor");

    // Lux panel background
    tft.fillRoundRect(LUX_PAN_X, PANEL_Y, LUX_PAN_W, PANEL_H, 6, C(10, 10, 45));
    tft.setTextColor(C(120, 180, 255));
    tft.setTextSize(1);
    tft.setCursor(LUX_PAN_X + 7, PANEL_Y + 6);
    tft.print("ILLUMINANCE (lux)");

    // MCU panel background
    tft.fillRoundRect(MCU_PAN_X, PANEL_Y, MCU_PAN_W, PANEL_H, 6, C(10, 45, 10));
    tft.setTextColor(C(120, 220, 120));
    tft.setTextSize(1);
    tft.setCursor(MCU_PAN_X + 7, PANEL_Y + 6);
    tft.print("MCU STATUS");

    // Status bar background
    tft.fillRect(0, STATUS_Y, SCREEN_W, STATUS_H, C(22, 22, 22));
}

// =============================================================================
// Illuminance widget
// =============================================================================
static void updateLuxWidget()
{
    const int16_t px = LUX_PAN_X + 1;
    const int16_t py = PANEL_Y + 18;
    const int16_t pw = LUX_PAN_W - 2;

    // Clear value area
    tft.fillRect(px, py, pw, 86, C(10, 10, 45));

    // ── Numeric lux value ──
    char buf[16];
    if (g_lux < 10000.0f) {
        snprintf(buf, sizeof(buf), "%.1f", g_lux);
    } else {
        snprintf(buf, sizeof(buf), "%.0f", g_lux);
    }
    // Trim leading spaces
    const char *num = buf;
    while (*num == ' ') num++;

    tft.setTextSize(3);
    tft.setTextColor(C(255, 220, 0));

    int16_t tx, ty;
    uint16_t tw, th;
    tft.getTextBounds(num, 0, 0, &tx, &ty, &tw, &th);
    int16_t nx = px + ((int16_t)pw - (int16_t)tw) / 2;
    tft.setCursor(max((int16_t)px, nx), py + 4);
    tft.print(num);

    // ── Unit label ──
    tft.setTextSize(2);
    tft.setTextColor(C(255, 180, 80));
    tft.setCursor(px + (pw - 36) / 2, py + 42);
    tft.print("lux");

    // ── Progress bar ──
    const int16_t bx = px + 2;
    const int16_t by = py + 68;
    const int16_t bw = pw - 4;
    const int16_t bh = 8;
    tft.fillRect(bx, by, bw, bh, C(40, 40, 65));
    float ratio = (g_lux - LUX_DIM) / (LUX_BRIGHT - LUX_DIM);
    ratio = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
    int16_t fill = (int16_t)(ratio * bw);
    if (fill > 0) {
        tft.fillRect(bx, by, fill, bh, C(255, 210, 0));
    }
    tft.drawRect(bx, by, bw, bh, C(80, 80, 110));
}

// =============================================================================
// MCU status widget
// =============================================================================
static void updateMcuWidget()
{
    const int16_t px = MCU_PAN_X + 1;
    const int16_t py = PANEL_Y + 18;
    const int16_t pw = MCU_PAN_W - 2;

    tft.fillRect(px, py, pw, 100, C(10, 45, 10));

    tft.setTextSize(1);
    int16_t xo = px + 4;
    int16_t yo = py + 2;
    const int16_t LH = 14;   // line height

    // ── Uptime ──
    uint32_t upSec = (millis() - g_startMs) / 1000UL;
    char upBuf[24];
    snprintf(upBuf, sizeof(upBuf), "UP  %02lu:%02lu:%02lu",
             (unsigned long)(upSec / 3600UL),
             (unsigned long)((upSec % 3600UL) / 60UL),
             (unsigned long)(upSec % 60UL));
    tft.setTextColor(C(210, 210, 210));
    tft.setCursor(xo, yo); tft.print(upBuf); yo += LH;

    // ── CPU frequency ──
    char cpuBuf[20];
    snprintf(cpuBuf, sizeof(cpuBuf), "CPU  %lu MHz",
             (unsigned long)(SystemCoreClock / 1000000UL));
    tft.setTextColor(C(150, 220, 150));
    tft.setCursor(xo, yo); tft.print(cpuBuf); yo += LH;

    // ── RAM (RP2040 has 264 KB SRAM) ──
    tft.setCursor(xo, yo); tft.print("RAM  264 KB"); yo += LH;

    // ── Sensor status ──
    tft.setTextColor(g_sensorOk ? C(80, 255, 80) : C(255, 80, 80));
    tft.setCursor(xo, yo);
    tft.print(g_sensorOk ? "SENS OK" : "SENS ERR"); yo += LH;

    // ── Core ──
    tft.setTextColor(C(100, 150, 255));
    tft.setCursor(xo, yo); tft.print("CORE Dual M0+"); yo += LH;

    // ── Backlight % ──
    tft.setTextColor(C(200, 200, 100));
    char blBuf[16];
    uint8_t pct = (uint8_t)((uint16_t)(g_brightness - BL_MIN) * 100u
                             / (BL_MAX - BL_MIN));
    snprintf(blBuf, sizeof(blBuf), "BL   %3u%%", pct);
    tft.setCursor(xo, yo); tft.print(blBuf);
}

// =============================================================================
// Status bar (bottom strip)
// =============================================================================
static void updateStatusBar()
{
    tft.fillRect(0, STATUS_Y, SCREEN_W, STATUS_H, C(22, 22, 22));
    tft.setTextSize(1);

    // ── Lux range label ──
    const char *rangeStr;
    uint16_t    rangeCol;
    if      (g_lux <   10.0f) { rangeStr = "DARK";     rangeCol = C(80,  80,  200); }
    else if (g_lux <  100.0f) { rangeStr = "DIM";      rangeCol = C(140, 140, 255); }
    else if (g_lux <  500.0f) { rangeStr = "INDOOR";   rangeCol = C(255, 255, 100); }
    else if (g_lux < 2000.0f) { rangeStr = "BRIGHT";   rangeCol = C(255, 200,  50); }
    else                       { rangeStr = "SUNLIGHT"; rangeCol = C(255, 140,  40); }

    tft.setTextColor(C(150, 150, 150));
    tft.setCursor(6, STATUS_Y + 9);
    tft.print("LUX:");
    tft.setTextColor(rangeCol);
    tft.setCursor(34, STATUS_Y + 9);
    tft.print(rangeStr);

    // ── Separator ──
    tft.setTextColor(C(60, 60, 60));
    tft.setCursor(90, STATUS_Y + 9);
    tft.print("|");

    // ── Backlight % ──
    tft.setTextColor(C(150, 150, 150));
    tft.setCursor(100, STATUS_Y + 9);
    tft.print("BL:");
    char blPct[8];
    uint8_t pct = (uint8_t)((uint16_t)(g_brightness - BL_MIN) * 100u
                             / (BL_MAX - BL_MIN));
    snprintf(blPct, sizeof(blPct), "%3u%%", pct);
    tft.setTextColor(C(220, 220, 100));
    tft.setCursor(118, STATUS_Y + 9);
    tft.print(blPct);

    // ── Separator ──
    tft.setTextColor(C(60, 60, 60));
    tft.setCursor(154, STATUS_Y + 9);
    tft.print("|");

    // ── Uptime (right area) ──
    uint32_t upSec = (millis() - g_startMs) / 1000UL;
    char tBuf[20];
    snprintf(tBuf, sizeof(tBuf), "T+%lus", (unsigned long)upSec);
    tft.setTextColor(C(100, 100, 100));
    tft.setCursor(162, STATUS_Y + 9);
    tft.print(tBuf);
}

// =============================================================================
// Backlight PWM linked to APDS-9930 lux
// =============================================================================
static void applyBacklight(float luxVal)
{
    float clamped = luxVal < LUX_DIM     ? LUX_DIM
                  : luxVal > LUX_BRIGHT  ? LUX_BRIGHT
                  : luxVal;
    float ratio   = (clamped - LUX_DIM) / (LUX_BRIGHT - LUX_DIM);
    int   target  = (int)(BL_MIN + ratio * (float)(BL_MAX - BL_MIN));
    g_brightness  = (uint8_t)(target < BL_MIN ? BL_MIN
                             : target > BL_MAX ? BL_MAX : target);
    analogWrite(TFT_BL, g_brightness);
}
