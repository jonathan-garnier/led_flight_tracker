// Based on official 1_SimpleTestShapes example from
// https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA
//
// Adapted for ESP-IDF with custom pin mapping for 64x32 panel

#include "display.h"
#include "config_storage.h"
#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include "Arduino.h"
#include "esp_log.h"
#include <time.h>
#include <Fonts/TomThumb.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include "fonts/RouteBold.h"

static const char *TAG = "display";

#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

static MatrixPanel_I2S_DMA *dma_display = nullptr;

// Theme colours (stored as color565, updated via display_set_color_theme)
static uint16_t s_clr_route    = 0xFFFF;  // white (route row)
static uint16_t s_clr_callsign = 0x07FF;  // cyan  (callsign row)
static uint16_t s_clr_speed    = 0xFFE0;  // yellow
static uint16_t s_clr_counter  = 0xF81F;  // magenta (approximate)

// Fixed helicopter colours (shown instead of the route row for rotorcraft).
#define HELI_BODY_565  0xCE59   // light grey  (~200,200,200)
#define HELI_ROTOR_565 0x7E5F   // bright cyan-white (~120,200,255)

void display_arduino_init(void)
{
    // Initialize Arduino HAL - MUST run before nvs_flash_init()
    initArduino();
    ESP_LOGI(TAG, "Arduino HAL initialized");
}

void display_init(void)
{
    // Custom pin mapping
    HUB75_I2S_CFG::i2s_pins pins = {
        .r1 = 2,
        .g1 = 4,   // swapped: panel B/G channels are reversed
        .b1 = 15,
        .r2 = 5,
        .g2 = 7,   // swapped: panel B/G channels are reversed
        .b2 = 6,
        .a = 8,
        .b = 9,
        .c = 10,
        .d = 11,
        .e = -1,
        .lat = 13,
        .oe = 14,
        .clk = 12,
    };

    HUB75_I2S_CFG mxconfig(
        PANEL_RES_X,   // module width
        PANEL_RES_Y,   // module height
        PANEL_CHAIN,   // chain length
        pins           // custom pin mapping
    );

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();

    uint8_t brightness = 90;
    config_storage_get_brightness(&brightness);
    dma_display->setBrightness8(brightness);
    dma_display->clearScreen();
    ESP_LOGI(TAG, "Brightness: %d", brightness);

    ESP_LOGI(TAG, "Display initialized (64x32, 1/16 scan)");
}

// Matches the official 1_SimpleTestShapes example
void display_test_pattern(void)
{
    if (!dma_display) return;

    uint16_t myBLACK = dma_display->color565(0, 0, 0);
    uint16_t myWHITE = dma_display->color565(255, 255, 255);
    uint16_t myRED   = dma_display->color565(255, 0, 0);
    uint16_t myGREEN = dma_display->color565(0, 255, 0);
    uint16_t myBLUE  = dma_display->color565(0, 0, 255);

    // Step 1: Fill entire screen WHITE
    ESP_LOGI(TAG, "Step 1: fillScreen WHITE");
    dma_display->fillScreen(myWHITE);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 2: Fill entire screen GREEN
    ESP_LOGI(TAG, "Step 2: fillRect GREEN (full screen)");
    dma_display->fillRect(0, 0, dma_display->width(), dma_display->height(), dma_display->color444(0, 15, 0));
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 3: Draw yellow border
    ESP_LOGI(TAG, "Step 3: drawRect YELLOW border");
    dma_display->drawRect(0, 0, dma_display->width(), dma_display->height(), dma_display->color444(15, 15, 0));
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 4: Draw red X
    ESP_LOGI(TAG, "Step 4: drawLine RED X");
    dma_display->drawLine(0, 0, dma_display->width()-1, dma_display->height()-1, dma_display->color444(15, 0, 0));
    dma_display->drawLine(dma_display->width()-1, 0, 0, dma_display->height()-1, dma_display->color444(15, 0, 0));
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 5: Blue circle
    ESP_LOGI(TAG, "Step 5: drawCircle BLUE");
    dma_display->drawCircle(10, 10, 10, dma_display->color444(0, 0, 15));
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 6: Violet filled circle
    ESP_LOGI(TAG, "Step 6: fillCircle VIOLET");
    dma_display->fillCircle(40, 21, 10, dma_display->color444(15, 0, 15));
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 7: Clear and show text (GFX println)
    ESP_LOGI(TAG, "Step 7: Text test");
    dma_display->fillScreen(myBLACK);
    dma_display->setTextSize(1);
    dma_display->setTextWrap(false);
    dma_display->setCursor(5, 0);
    dma_display->setTextColor(myWHITE);
    dma_display->println("ESP32 DMA");
    dma_display->setTextColor(myRED);
    dma_display->println("LED MATRIX!");
    dma_display->setTextColor(myGREEN);
    dma_display->println("64x32 Test");
    dma_display->setTextColor(myBLUE);
    dma_display->println("Pattern OK");

    ESP_LOGI(TAG, "Test pattern complete - check display!");
}

// Helper to calculate centered X using actual text bounds from GFX
static int center_text_x(MatrixPanel_I2S_DMA *display, const char *text)
{
    int16_t x1, y1;
    uint16_t w, h;
    display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    int x = (64 - (int)w) / 2;
    return (x < 0) ? 0 : x;
}

// ---- Flight layout: route (big, top) / callsign (mid) / speed+counter (bottom) ----

#define ROUTE_BASELINE 13   // RouteBold glyphs span y=1..12
#define CALLSIGN_Y     15   // default-font top row (where the country used to be)
#define ROW3_Y         24   // speed + counter (unchanged)

static void draw_speed_counter(const flight_t *flight, int index, int total)
{
    dma_display->setFont(NULL);
    dma_display->setTextSize(1);

    int kmh = (int)(flight->velocity * 3.6f + 0.5f);
    char spd_str[16];
    snprintf(spd_str, sizeof(spd_str), "%dkm/h", kmh);
    dma_display->setCursor(1, ROW3_Y);
    dma_display->setTextColor(s_clr_speed);
    dma_display->print(spd_str);

    char counter[24];
    snprintf(counter, sizeof(counter), "%d/%d", index + 1, total);
    int counter_x = 64 - (int)(strlen(counter) * 6);
    dma_display->setCursor(counter_x, ROW3_Y);
    dma_display->setTextColor(s_clr_counter);
    dma_display->print(counter);
}

static void draw_callsign_row(const flight_t *flight)
{
    dma_display->setFont(NULL);
    dma_display->setTextSize(1);

    if (flight->callsign[0] == '\0') {
        const char *ph = "Unknown";
        int x = (64 - (int)strlen(ph) * 6) / 2;
        if (x < 0) x = 0;
        dma_display->setCursor(x, CALLSIGN_Y);
        dma_display->setTextColor(dma_display->color565(180, 180, 180));
        dma_display->print(ph);
    } else {
        // Callsign is <=8 chars -> <=48px in the default font, always fits.
        int x = (64 - (int)strlen(flight->callsign) * 6) / 2;
        if (x < 0) x = 0;
        dma_display->setCursor(x, CALLSIGN_Y);
        dma_display->setTextColor(s_clr_callsign);
        dma_display->print(flight->callsign);
    }
}

// Draw "ORG [triangle] DST" centered with the given font/size.
// Returns false (drawing nothing) if it would exceed the panel width.
static bool try_draw_route(const char *o, const char *d, const GFXfont *font, int tsize)
{
    // Arrow block = gap + triangle + gap. getTextBounds gives ink width, which
    // is within ~1px of the advance width we actually draw with, so a total of
    // <=62 here keeps the real drawn width <=64 (fits the panel).
    const int gap = 1, aw = 6;
    int16_t bx, by;
    uint16_t wO, wD, h;
    dma_display->setFont(font);
    dma_display->setTextSize(tsize);
    dma_display->getTextBounds(o, 0, 0, &bx, &by, &wO, &h);
    dma_display->getTextBounds(d, 0, 0, &bx, &by, &wD, &h);

    int total = (int)wO + gap + aw + gap + (int)wD;
    if (total > 62) return false;   // leave margin; caller falls back

    int x = (64 - total) / 2;
    if (x < 0) x = 0;
    dma_display->setTextColor(s_clr_route);
    dma_display->setCursor(x, ROUTE_BASELINE);
    dma_display->print(o);

    int arrowX = dma_display->getCursorX() + gap;
    int ttop = ROUTE_BASELINE - 11;   // ~y=2
    int tbot = ROUTE_BASELINE - 2;    // ~y=11
    int tmid = (ttop + tbot) / 2;
    dma_display->fillTriangle(arrowX, ttop, arrowX, tbot, arrowX + aw, tmid, s_clr_route);

    dma_display->setCursor(arrowX + aw + gap, ROUTE_BASELINE);
    dma_display->print(d);
    return true;
}

static void draw_route_row(const flight_t *flight)
{
    if (flight->route_status == ROUTE_RESOLVED &&
        flight->origin[0] && flight->dest[0]) {
        // Custom bold font, falling back to a narrower TomThumb x2 if too wide.
        if (!try_draw_route(flight->origin, flight->dest, &RouteBold, 1)) {
            try_draw_route(flight->origin, flight->dest, &TomThumb, 2);
        }
        return;
    }

    // Pending or no-route: a dim placeholder in the big row.
    const char *txt = (flight->route_status == ROUTE_PENDING) ? "..." : "-";
    int16_t bx, by;
    uint16_t w, h;
    dma_display->setFont(&RouteBold);
    dma_display->setTextSize(1);
    dma_display->getTextBounds(txt, 0, 0, &bx, &by, &w, &h);
    int x = (64 - (int)w) / 2;
    if (x < 0) x = 0;
    dma_display->setCursor(x, ROUTE_BASELINE);
    dma_display->setTextColor(dma_display->color565(90, 90, 90));
    dma_display->print(txt);
}

// ---- Helicopter sprite (shown instead of the route row for rotorcraft) ----
//
// Small left-facing helicopter living entirely in the top route band (y<=14,
// above CALLSIGN_Y). The rotor animates over a few "blade phases" to read as a
// spinning 2-blade rotor at 64x32 scale.

#define HELI_HUB_X     28          // rotor hub / mast x (sprite centred ~x=32)
#define HELI_HUB_Y     2           // rotor bar y
#define HELI_ROTOR_H   6           // rows cleared/redrawn each animation frame (y=0..5)

static void draw_heli_body(void)
{
    uint16_t body = HELI_BODY_565;

    // Cabin (rounded), tail boom, vertical fin + tail-rotor nub.
    dma_display->fillRoundRect(21, 6, 14, 7, 3, body);  // cabin, nose to the left
    dma_display->fillRect(35, 8, 12, 2, body);          // tail boom
    dma_display->fillRect(46, 5, 2, 5, body);           // tail fin
    dma_display->drawFastHLine(45, 4, 3, body);         // tail rotor nub

    // Landing skids: a rail beneath the cabin with two struts.
    dma_display->drawFastHLine(20, 13, 15, body);       // skid rail
    dma_display->drawFastVLine(23, 11, 2, body);        // front strut
    dma_display->drawFastVLine(31, 11, 2, body);        // rear strut
}

// Clears the top rotor strip and redraws the mast + rotor at the given phase.
// Cycling phase 0..3 fakes a spinning 2-blade rotor.
static void draw_heli_rotor(int phase)
{
    uint16_t body  = HELI_BODY_565;
    uint16_t rotor = HELI_ROTOR_565;
    int hx = HELI_HUB_X, hy = HELI_HUB_Y;

    // Erase just the strip above the cabin (cabin starts at y=6).
    dma_display->fillRect(0, 0, 64, HELI_ROTOR_H, 0);

    // Mast connecting hub down to the cabin roof.
    dma_display->drawFastVLine(hx, hy + 1, 4, body);    // y=3..6

    switch (phase & 3) {
    case 0:  // blades sideways: long flat bar
        dma_display->drawLine(hx - 11, hy, hx + 11, hy, rotor);
        break;
    case 1:  // rotating: medium bar tilted down-to-up
        dma_display->drawLine(hx - 7, hy + 1, hx + 7, hy - 1, rotor);
        break;
    case 2:  // nearly edge-on: short bar
        dma_display->drawLine(hx - 3, hy, hx + 3, hy, rotor);
        break;
    default: // rotating: medium bar tilted up-to-down
        dma_display->drawLine(hx - 7, hy - 1, hx + 7, hy + 1, rotor);
        break;
    }
    dma_display->drawPixel(hx, hy, rotor);              // hub
}

static inline bool flight_is_helicopter(const flight_t *flight)
{
    return flight->category == FLIGHT_CATEGORY_ROTORCRAFT;
}

static void render_flight(const flight_t *flight, int index, int total)
{
    dma_display->fillScreen(0);
    dma_display->setTextWrap(false);

    if (flight_is_helicopter(flight)) {
        draw_heli_body();
        draw_heli_rotor(0);   // static phase-0 rotor; animation cycles it
    } else {
        draw_route_row(flight);
    }
    draw_callsign_row(flight);
    draw_speed_counter(flight, index, total);

    ESP_LOGI(TAG, "Flight %d/%d: %s [%s>%s st=%d] %dkm/h",
             index + 1, total, flight->callsign,
             flight->origin[0] ? flight->origin : "?",
             flight->dest[0] ? flight->dest : "?",
             flight->route_status,
             (int)(flight->velocity * 3.6f + 0.5f));
}

void display_show_flight(const flight_t *flight, int index, int total)
{
    if (!dma_display) return;
    render_flight(flight, index, total);
}

void display_show_no_flights(void)
{
    if (!dma_display) return;

    uint16_t black = dma_display->color565(0, 0, 0);
    uint16_t cyan  = dma_display->color565(0, 255, 255);

    dma_display->fillScreen(black);
    dma_display->setFont(NULL);
    dma_display->setTextSize(1);
    dma_display->setTextWrap(false);
    dma_display->setTextColor(cyan);

    const char *l1 = "No flights";
    int l1_x = center_text_x(dma_display, l1);
    dma_display->setCursor(l1_x, 8);
    dma_display->print(l1);

    const char *l2 = "overhead";
    int l2_x = center_text_x(dma_display, l2);
    dma_display->setCursor(l2_x, 18);
    dma_display->print(l2);
}

// --- Large 7-segment clock rendering ---

#define SEG_A 0x01
#define SEG_B 0x02
#define SEG_C 0x04
#define SEG_D 0x08
#define SEG_E 0x10
#define SEG_F 0x20
#define SEG_G 0x40

static const uint8_t DIGIT_SEGS[10] = {
    SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F,        // 0
    SEG_B|SEG_C,                                   // 1 (special-cased)
    SEG_A|SEG_B|SEG_D|SEG_E|SEG_G,               // 2
    SEG_A|SEG_B|SEG_C|SEG_D|SEG_G,               // 3
    SEG_B|SEG_C|SEG_F|SEG_G,                      // 4
    SEG_A|SEG_C|SEG_D|SEG_F|SEG_G,               // 5
    SEG_A|SEG_C|SEG_D|SEG_E|SEG_F|SEG_G,         // 6
    SEG_A|SEG_B|SEG_C,                            // 7
    SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F|SEG_G,  // 8
    SEG_A|SEG_B|SEG_C|SEG_D|SEG_F|SEG_G,         // 9
};

// Digit is 11px wide × 25px tall, centred at y=4, 3px thick segments
// Verticals extend through the middle zone so digits like 0 have no gap
struct seg_rect { int x, y, w, h; };

static const seg_rect SEG_GEOM[7] = {
    {0,  4,  11, 3},   // A - top      (y=4..6)
    {8,  7,  3,  10},  // B - upper right (y=7..16, bridges middle)
    {8,  16, 3,  10},  // C - lower right (y=16..25)
    {0,  26, 11, 3},   // D - bottom   (y=26..28)
    {0,  16, 3,  10},  // E - lower left
    {0,  7,  3,  10},  // F - upper left
    {0,  15, 11, 3},   // G - middle   (y=15..17)
};

static uint16_t clock_row_color(int y)
{
    if (y < 7)  return dma_display->color565(0, 200, 255);    // cyan
    if (y < 13) return dma_display->color565(0, 220, 60);     // green
    if (y < 19) return dma_display->color565(220, 220, 0);    // yellow
    if (y < 25) return dma_display->color565(255, 120, 0);    // orange
    return dma_display->color565(255, 0, 40);                  // red
}

static void draw_big_digit(int dx, int digit)
{
    // "1" drawn as a centred bar spanning the digit height
    if (digit == 1) {
        int bar_x = dx + 4;  // centre 3px bar in 11px digit
        for (int y = 4; y <= 28; y++) {
            dma_display->drawFastHLine(bar_x, y, 3, clock_row_color(y));
        }
        return;
    }

    // "4" special-cased: extend verticals to full height so it matches others
    if (digit == 4) {
        // F - left vertical, top half only (y=4..17)
        for (int y = 4; y <= 17; y++)
            dma_display->drawFastHLine(dx + 0, y, 3, clock_row_color(y));
        // G - middle horizontal
        for (int y = 15; y <= 17; y++)
            dma_display->drawFastHLine(dx + 0, y, 11, clock_row_color(y));
        // B+C - right vertical, full height (y=4..28)
        for (int y = 4; y <= 28; y++)
            dma_display->drawFastHLine(dx + 8, y, 3, clock_row_color(y));
        return;
    }

    // "7" special-cased: extend right vertical to full height
    if (digit == 7) {
        // A - top horizontal
        for (int y = 4; y <= 6; y++)
            dma_display->drawFastHLine(dx + 0, y, 11, clock_row_color(y));
        // B+C - right vertical, full height (y=4..28)
        for (int y = 4; y <= 28; y++)
            dma_display->drawFastHLine(dx + 8, y, 3, clock_row_color(y));
        return;
    }

    // digit 0-9 draws the number, anything else draws a dash (segment G only)
    uint8_t segs = (digit >= 0 && digit <= 9) ? DIGIT_SEGS[digit] : SEG_G;

    for (int s = 0; s < 7; s++) {
        if (!(segs & (1 << s))) continue;
        const seg_rect *r = &SEG_GEOM[s];
        for (int row = 0; row < r->h; row++) {
            int y = r->y + row;
            dma_display->drawFastHLine(dx + r->x, y, r->w, clock_row_color(y));
        }
    }
}

// Block-letter bitmaps for AM/PM (white, 2px-thick strokes)
// P and A are 6 columns wide, M is 7 columns wide, all 9 rows tall
#define LETTER_H 9

static const uint8_t LETTER_P[LETTER_H] = {
    0x3E, 0x3E,  // #####.
    0x33, 0x33,  // ##..##
    0x3E, 0x3E,  // #####.
    0x30, 0x30,  // ##....
    0x30,        // ##....
};

static const uint8_t LETTER_A[LETTER_H] = {
    0x1E,        // .####.
    0x33, 0x33,  // ##..##
    0x3F, 0x3F,  // ######
    0x33, 0x33,  // ##..##
    0x33, 0x33,  // ##..##
};

static const uint8_t LETTER_M[LETTER_H] = {
    0x63,        // ##...##
    0x77,        // ###.###
    0x7F,        // #######
    0x6B,        // ##.#.##
    0x63, 0x63,  // ##...##
    0x63, 0x63,  // ##...##
    0x63,        // ##...##
};

static void draw_block_letter(int dx, int dy, const uint8_t *bitmap, int cols)
{
    uint16_t white = dma_display->color565(255, 255, 255);
    for (int row = 0; row < LETTER_H; row++) {
        int y = dy + row;
        uint8_t bits = bitmap[row];
        for (int col = 0; col < cols; col++) {
            if (bits & (1 << (cols - 1 - col))) {
                dma_display->drawPixel(dx + col, y, white);
            }
        }
    }
}

static int s_prev_clock_h = -1;
static int s_prev_clock_m = -1;

static void draw_clock_frame(void)
{
    if (!dma_display) return;

    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);

    bool time_valid = (ti.tm_year >= (2024 - 1900));

    // 12-hour conversion
    int h24 = ti.tm_hour;
    bool is_pm = (h24 >= 12);
    int h12 = h24 % 12;
    if (h12 == 0) h12 = 12;

    // Skip redraw if nothing changed (we don't show seconds)
    if (h12 == s_prev_clock_h && ti.tm_min == s_prev_clock_m) {
        return;
    }
    s_prev_clock_h = h12;
    s_prev_clock_m = ti.tm_min;

    dma_display->fillScreen(0);

    // Layout: 11px digits (3px segs), 3px colon, 6+7px block letters
    const int d1x = -3, d2x = 8, d3x = 24, d4x = 36;
    const int colon_x = 20;
    const int ltr1_x = 49, ltr2_x = 56;
    const int ltr_y = (32 - LETTER_H) / 2;  // vertically centred

    if (time_valid) {
        if (h12 >= 10) draw_big_digit(d1x, h12 / 10);
        draw_big_digit(d2x, h12 % 10);
        draw_big_digit(d3x, ti.tm_min / 10);
        draw_big_digit(d4x, ti.tm_min % 10);
    } else {
        draw_big_digit(d1x, -1);
        draw_big_digit(d2x, -1);
        draw_big_digit(d3x, -1);
        draw_big_digit(d4x, -1);
    }

    // Colon (always visible, 3×3 dots)
    dma_display->fillRect(colon_x, 10, 3, 3, clock_row_color(11));
    dma_display->fillRect(colon_x, 19, 3, 3, clock_row_color(20));

    // AM/PM in white block letters
    draw_block_letter(ltr1_x, ltr_y, is_pm ? LETTER_P : LETTER_A, 6);
    draw_block_letter(ltr2_x, ltr_y, LETTER_M, 7);
}

static void draw_bonsai(void)
{
    if (!dma_display) return;

    uint16_t pot_rim   = dma_display->color565(120, 55, 15);
    uint16_t pot_body  = dma_display->color565(155, 75, 30);
    uint16_t trunk_col = dma_display->color565(95, 55, 15);
    uint16_t leaf_dk   = dma_display->color565(0, 75, 20);
    uint16_t leaf_md   = dma_display->color565(15, 115, 30);
    uint16_t leaf_lt   = dma_display->color565(35, 155, 45);

    dma_display->fillScreen(0);

    // Pot
    dma_display->fillRect(21, 26, 22, 1, pot_rim);
    dma_display->fillRect(23, 27, 18, 4, pot_body);
    dma_display->fillRect(25, 31, 14, 1, pot_rim);

    // Trunk base
    dma_display->fillRect(31, 23, 3, 3, trunk_col);

    // Main trunk curving up-right then left
    dma_display->drawLine(32, 23, 35, 17, trunk_col);
    dma_display->drawLine(33, 23, 36, 17, trunk_col);
    dma_display->drawLine(31, 23, 34, 17, trunk_col);

    // Upper trunk curving left
    dma_display->drawLine(35, 17, 28, 9, trunk_col);
    dma_display->drawLine(34, 17, 27, 9, trunk_col);

    // Right branch
    dma_display->drawLine(34, 16, 42, 10, trunk_col);
    dma_display->drawLine(35, 16, 43, 10, trunk_col);

    // Left branch
    dma_display->drawLine(30, 13, 22, 7, trunk_col);
    dma_display->drawLine(29, 13, 21, 7, trunk_col);

    // Foliage pads - dark base, medium, light highlights
    // Main canopy (top-left)
    dma_display->fillCircle(27, 5, 6, leaf_dk);
    dma_display->fillCircle(25, 3, 5, leaf_md);
    dma_display->fillCircle(28, 3, 4, leaf_lt);

    // Right pad
    dma_display->fillCircle(43, 7, 5, leaf_dk);
    dma_display->fillCircle(42, 5, 4, leaf_md);
    dma_display->fillCircle(44, 5, 3, leaf_lt);

    // Left pad
    dma_display->fillCircle(20, 5, 4, leaf_dk);
    dma_display->fillCircle(19, 4, 3, leaf_md);
    dma_display->fillCircle(21, 3, 2, leaf_lt);

    // Small connecting pad at top
    dma_display->fillCircle(35, 2, 3, leaf_dk);
    dma_display->fillCircle(36, 1, 2, leaf_md);
}

void display_show_idle(int duration_ms)
{
    if (!dma_display) return;

    uint8_t mode = IDLE_MODE_TEXT;
    config_storage_get_idle_mode(&mode);

    switch (mode) {
    case IDLE_MODE_CLOCK: {
        s_prev_clock_h = -1;  // force first frame to draw
        s_prev_clock_m = -1;
        int elapsed = 0;
        int step_ms = 1000;
        while (elapsed < duration_ms) {
            draw_clock_frame();
            int remaining = duration_ms - elapsed;
            int wait = remaining < step_ms ? remaining : step_ms;
            vTaskDelay(pdMS_TO_TICKS(wait));
            elapsed += wait;
        }
        break;
    }
    case IDLE_MODE_BONSAI:
        draw_bonsai();
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        break;
    default:
        display_show_no_flights();
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        break;
    }
}

void display_show_status(const char *line1, const char *line2)
{
    if (!dma_display) return;

    uint16_t black  = dma_display->color565(0, 0, 0);
    uint16_t yellow = dma_display->color565(255, 255, 0);

    dma_display->fillScreen(black);
    dma_display->setTextSize(1);
    dma_display->setTextWrap(false);
    dma_display->setTextColor(yellow);

    if (line1) {
        dma_display->setCursor(1, 8);
        dma_display->print(line1);
    }
    if (line2) {
        dma_display->setCursor(1, 18);
        dma_display->print(line2);
    }
}

void display_show_status_scroll(const char *line1, const char *line2, int duration_ms)
{
    if (!dma_display) return;

    uint16_t black  = dma_display->color565(0, 0, 0);
    uint16_t yellow = dma_display->color565(255, 255, 0);

    dma_display->fillScreen(black);
    dma_display->setFont(NULL);
    dma_display->setTextSize(1);
    dma_display->setTextWrap(false);

    // Static top line
    dma_display->setTextColor(yellow);
    if (line1) {
        dma_display->setCursor(1, 8);
        dma_display->print(line1);
    }

    const int y2 = 18;
    int line2_pw = line2 ? (int)strlen(line2) * 6 : 0;  // default font 6px/char
    int scroll_range = line2_pw - 64;

    // Fits as-is: just centre it and hold
    if (!line2 || scroll_range <= 0) {
        if (line2) {
            int x = (64 - line2_pw) / 2;
            if (x < 0) x = 0;
            dma_display->setTextColor(yellow);
            dma_display->setCursor(x, y2);
            dma_display->print(line2);
        }
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }

    const int frame_ms = 40;
    const int end_hold_ms = 900;
    int elapsed = 0;
    int prev_x = 0;

    // Initial draw at start position
    dma_display->setTextColor(yellow);
    dma_display->setCursor(prev_x, y2);
    dma_display->print(line2);

    while (elapsed < duration_ms) {
        // Hold at start
        vTaskDelay(pdMS_TO_TICKS(end_hold_ms));
        elapsed += end_hold_ms;

        // Scroll left
        for (int x = -1; x >= -scroll_range && elapsed < duration_ms; x--) {
            dma_display->setTextColor(black);   // erase previous
            dma_display->setCursor(prev_x, y2);
            dma_display->print(line2);
            dma_display->setTextColor(yellow);  // draw new
            dma_display->setCursor(x, y2);
            dma_display->print(line2);
            prev_x = x;
            vTaskDelay(pdMS_TO_TICKS(frame_ms));
            elapsed += frame_ms;
        }

        // Hold at end
        vTaskDelay(pdMS_TO_TICKS(end_hold_ms));
        elapsed += end_hold_ms;

        // Jump back to start for the next pass
        dma_display->setTextColor(black);
        dma_display->setCursor(prev_x, y2);
        dma_display->print(line2);
        dma_display->setTextColor(yellow);
        dma_display->setCursor(0, y2);
        dma_display->print(line2);
        prev_x = 0;
    }
}

void display_show_config_mode(void)
{
    if (!dma_display) return;

    uint16_t black  = dma_display->color565(0, 0, 0);
    uint16_t cyan   = dma_display->color565(0, 255, 255);
    uint16_t white  = dma_display->color565(255, 255, 255);

    const char *line1 = "Join WiFi:";
    const char *ap_name = "FlightTracker";
    const int ap_name_pw = (int)strlen(ap_name) * 6;  // default font 6px/char
    const int line2_y = 18;
    const int scroll_range = ap_name_pw - 64;

    dma_display->fillScreen(black);
    dma_display->setFont(NULL);
    dma_display->setTextSize(1);
    dma_display->setTextWrap(false);

    // Line 1: static
    dma_display->setCursor(1, 4);
    dma_display->setTextColor(cyan);
    dma_display->print(line1);

    if (scroll_range <= 0) {
        // Fits - just center it
        int x = (64 - ap_name_pw) / 2;
        dma_display->setCursor(x, line2_y);
        dma_display->setTextColor(white);
        dma_display->print(ap_name);
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    // Scroll loop forever
    while (1) {
        int prev_x = 0;

        // Draw at start position
        dma_display->setFont(NULL);
        dma_display->setTextSize(1);
        dma_display->setTextColor(white);
        dma_display->setCursor(prev_x, line2_y);
        dma_display->print(ap_name);
        vTaskDelay(pdMS_TO_TICKS(1500));

        // Scroll left
        int frame_ms = 30;
        int scroll_ms = 4000;
        int total_frames = scroll_ms / frame_ms;

        for (int frame = 1; frame <= total_frames; frame++) {
            int x = -(scroll_range * frame / total_frames);
            if (x != prev_x) {
                dma_display->setFont(NULL);
                dma_display->setTextSize(1);
                dma_display->setTextColor(black);
                dma_display->setCursor(prev_x, line2_y);
                dma_display->print(ap_name);
                dma_display->setTextColor(white);
                dma_display->setCursor(x, line2_y);
                dma_display->print(ap_name);
                prev_x = x;
            }
            vTaskDelay(pdMS_TO_TICKS(frame_ms));
        }

        vTaskDelay(pdMS_TO_TICKS(1500));

        // Scroll back right
        for (int frame = 1; frame <= total_frames; frame++) {
            int x = -(scroll_range * (total_frames - frame) / total_frames);
            if (x != prev_x) {
                dma_display->setFont(NULL);
                dma_display->setTextSize(1);
                dma_display->setTextColor(black);
                dma_display->setCursor(prev_x, line2_y);
                dma_display->print(ap_name);
                dma_display->setTextColor(white);
                dma_display->setCursor(x, line2_y);
                dma_display->print(ap_name);
                prev_x = x;
            }
            vTaskDelay(pdMS_TO_TICKS(frame_ms));
        }

        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

void display_set_brightness(uint8_t brightness)
{
    if (!dma_display) return;
    dma_display->setBrightness8(brightness);
    ESP_LOGI(TAG, "Brightness set to %d", brightness);
}

static uint16_t rgb_to_565(uint32_t rgb)
{
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void display_set_color_theme(uint32_t route_rgb, uint32_t callsign_rgb,
                             uint32_t speed_rgb, uint32_t counter_rgb)
{
    s_clr_route    = rgb_to_565(route_rgb);
    s_clr_callsign = rgb_to_565(callsign_rgb);
    s_clr_speed    = rgb_to_565(speed_rgb);
    s_clr_counter  = rgb_to_565(counter_rgb);
    ESP_LOGI(TAG, "Color theme updated");
}

void display_animate_flight(const flight_t *flight, int index, int total, int duration_ms)
{
    if (!dma_display) return;

    // Helicopters get an animated spinning rotor; everything else is static
    // (route fits by design, callsign <=8 chars fits, so nothing scrolls).
    if (flight_is_helicopter(flight)) {
        render_flight(flight, index, total);   // draws body + text once

        const int frame_ms = 110;              // ~9 fps -> ~2 rotor revs/sec
        int elapsed = 0;
        int phase = 0;
        while (elapsed < duration_ms) {
            draw_heli_rotor(phase++);
            int remaining = duration_ms - elapsed;
            int wait = remaining < frame_ms ? remaining : frame_ms;
            vTaskDelay(pdMS_TO_TICKS(wait));
            elapsed += wait;
        }
        return;
    }

    render_flight(flight, index, total);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
}
