// Based on official 1_SimpleTestShapes example from
// https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA
//
// Adapted for ESP-IDF with custom pin mapping for 64x32 panel

#include "display.h"
#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include "Arduino.h"
#include "esp_log.h"
#include <Fonts/TomThumb.h>
#include <Fonts/FreeSansBold9pt7b.h>

static const char *TAG = "display";

#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

static MatrixPanel_I2S_DMA *dma_display = nullptr;

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
        .g1 = 15,
        .b1 = 4,
        .r2 = 5,
        .g2 = 6,
        .b2 = 7,
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
    dma_display->setBrightness8(90);
    dma_display->clearScreen();

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

void display_show_flight(const flight_t *flight, int index, int total)
{
    if (!dma_display) return;

    uint16_t black  = dma_display->color565(0, 0, 0);
    uint16_t white  = dma_display->color565(255, 255, 255);
    uint16_t cyan   = dma_display->color565(0, 255, 255);
    uint16_t yellow = dma_display->color565(255, 255, 0);
    uint16_t magenta = dma_display->color565(255, 0, 200);

    dma_display->fillScreen(black);
    dma_display->setFont(&TomThumb);
    dma_display->setTextWrap(false);

    // Row 1: Callsign centered, as big as it fits
    if (flight->callsign[0] == '\0') {
        // No callsign - show placeholder in default font size 1
        dma_display->setFont(NULL);
        dma_display->setTextSize(1);
        const char *placeholder = "Unknown";
        int ph_x = (64 - (int)strlen(placeholder) * 6) / 2;
        dma_display->setCursor(ph_x, 4);
        dma_display->setTextColor(dma_display->color565(180, 180, 180));
        dma_display->print(placeholder);
        dma_display->setFont(&TomThumb);
    } else {
        // Try FreeSansBold9pt, fall back to TomThumb size 2 if too wide
        bool use_bold = true;
        dma_display->setFont(&FreeSansBold9pt7b);
        dma_display->setTextSize(1);
        int16_t x1, y1;
        uint16_t w, h;
        dma_display->getTextBounds(flight->callsign, 0, 0, &x1, &y1, &w, &h);
        if (w > 64) {
            dma_display->setFont(&TomThumb);
            dma_display->setTextSize(2);
            use_bold = false;
        }
        int callsign_x = center_text_x(dma_display, flight->callsign);
        dma_display->setCursor(callsign_x, use_bold ? 14 : 13);
        dma_display->setTextColor(white);
        dma_display->print(flight->callsign);
    }

    // Switch to default font for remaining rows
    dma_display->setFont(NULL);
    dma_display->setTextSize(1);

    // Row 2: Country centered
    int country_x = center_text_x(dma_display, flight->origin_country);
    dma_display->setCursor(country_x, 16);
    dma_display->setTextColor(cyan);
    dma_display->print(flight->origin_country);

    // Row 3 (y=27): Speed left, counter right
    int kmh = (int)(flight->velocity * 3.6f + 0.5f);
    char spd_str[16];
    snprintf(spd_str, sizeof(spd_str), "%dkm/h", kmh);
    dma_display->setCursor(1, 24);
    dma_display->setTextColor(yellow);
    dma_display->print(spd_str);

    char counter[24];
    snprintf(counter, sizeof(counter), "%d/%d", index + 1, total);
    int counter_x = 64 - (int)(strlen(counter) * 6);
    dma_display->setCursor(counter_x, 24);
    dma_display->setTextColor(magenta);
    dma_display->print(counter);

    ESP_LOGI(TAG, "Showing flight %d/%d: %s (%s) %.0fm %dkm/h",
             index + 1, total, flight->callsign, flight->origin_country,
             flight->altitude, kmh);
}

void display_show_no_flights(void)
{
    if (!dma_display) return;

    uint16_t black = dma_display->color565(0, 0, 0);
    uint16_t cyan  = dma_display->color565(0, 255, 255);

    dma_display->fillScreen(black);
    dma_display->setTextSize(1);
    dma_display->setTextWrap(false);
    dma_display->setTextColor(cyan);
    dma_display->setCursor(4, 8);
    dma_display->print("No flights");
    dma_display->setCursor(10, 18);
    dma_display->print("overhead");
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

void display_animate_flight(const flight_t *flight, int index, int total, int duration_ms)
{
    if (!dma_display) return;

    uint16_t black   = dma_display->color565(0, 0, 0);
    uint16_t white   = dma_display->color565(255, 255, 255);
    uint16_t cyan    = dma_display->color565(0, 255, 255);
    uint16_t yellow  = dma_display->color565(255, 255, 0);
    uint16_t magenta = dma_display->color565(255, 0, 200);

    dma_display->fillScreen(black);
    dma_display->setTextWrap(false);

    // --- Row 3: Speed left, counter right (static, never scrolls) ---
    dma_display->setFont(NULL);
    dma_display->setTextSize(1);

    int kmh = (int)(flight->velocity * 3.6f + 0.5f);
    char spd_str[16];
    snprintf(spd_str, sizeof(spd_str), "%dkm/h", kmh);
    dma_display->setCursor(1, 24);
    dma_display->setTextColor(yellow);
    dma_display->print(spd_str);

    char counter[24];
    snprintf(counter, sizeof(counter), "%d/%d", index + 1, total);
    int counter_x = 64 - (int)(strlen(counter) * 6);
    dma_display->setCursor(counter_x, 24);
    dma_display->setTextColor(magenta);
    dma_display->print(counter);

    // --- Measure country width (default font, 6px per char) ---
    const int country_y = 16;
    int country_pw = (int)strlen(flight->origin_country) * 6; // pixel width
    bool country_scrolls = country_pw > 64;
    int country_scroll_range = country_scrolls ? country_pw - 64 : 0;

    // --- Callsign row: FreeSansBold9pt ---
    const int callsign_y = 14;
    bool callsign_scrolls = false;
    int callsign_scroll_range = 0;
    int callsign_w_int = 0;

    if (flight->callsign[0] == '\0') {
        // No callsign - show "Unknown" in default font
        dma_display->setFont(NULL);
        dma_display->setTextSize(1);
        const char *placeholder = "Unknown";
        int ph_x = (64 - (int)strlen(placeholder) * 6) / 2;
        dma_display->setCursor(ph_x, 4);
        dma_display->setTextColor(dma_display->color565(180, 180, 180));
        dma_display->print(placeholder);
    } else {
        // Measure callsign width with FreeSansBold9pt
        dma_display->setFont(&FreeSansBold9pt7b);
        dma_display->setTextSize(1);
        int16_t bx1, by1;
        uint16_t callsign_w, callsign_h;
        dma_display->getTextBounds(flight->callsign, 0, 0, &bx1, &by1, &callsign_w, &callsign_h);
        callsign_w_int = (int)callsign_w;
        callsign_scrolls = callsign_w_int > 64;
        callsign_scroll_range = callsign_scrolls ? callsign_w_int - 64 : 0;
    }

    bool needs_animation = callsign_scrolls || country_scrolls;

    if (!needs_animation) {
        // --- Static display: draw everything centered, sleep ---
        if (flight->callsign[0] != '\0') {
            dma_display->setFont(&FreeSansBold9pt7b);
            dma_display->setTextSize(1);
            int callsign_x = (64 - callsign_w_int) / 2;
            dma_display->setCursor(callsign_x, callsign_y);
            dma_display->setTextColor(white);
            dma_display->print(flight->callsign);
        }

        dma_display->setFont(NULL);
        dma_display->setTextSize(1);
        int country_x = (64 - country_pw) / 2;
        dma_display->setCursor(country_x, country_y);
        dma_display->setTextColor(cyan);
        dma_display->print(flight->origin_country);

        ESP_LOGI(TAG, "Showing flight %d/%d: %s (%s) %.0fm %dkm/h",
                 index + 1, total, flight->callsign, flight->origin_country,
                 flight->altitude, kmh);

        vTaskDelay(pdMS_TO_TICKS(duration_ms));
    } else {
        // --- Animated display: scroll whichever text needs it ---
        int pause_ms = 1000;
        int scroll_ms = duration_ms - 2 * pause_ms;
        if (scroll_ms < 1000) scroll_ms = 1000;
        int frame_ms = 30;
        int total_frames = scroll_ms / frame_ms;

        ESP_LOGI(TAG, "Scrolling flight %d/%d: %s (%s) cs_scroll=%d co_scroll=%d",
                 index + 1, total, flight->callsign, flight->origin_country,
                 callsign_scroll_range, country_scroll_range);

        // Draw initial positions
        int cs_prev_x = callsign_scrolls ? 0 : (64 - callsign_w_int) / 2;
        int co_prev_x = country_scrolls ? 0 : (64 - country_pw) / 2;

        if (flight->callsign[0] != '\0') {
            dma_display->setFont(&FreeSansBold9pt7b);
            dma_display->setTextSize(1);
            dma_display->setTextColor(white);
            dma_display->setCursor(cs_prev_x, callsign_y);
            dma_display->print(flight->callsign);
        }

        dma_display->setFont(NULL);
        dma_display->setTextSize(1);
        dma_display->setTextColor(cyan);
        dma_display->setCursor(co_prev_x, country_y);
        dma_display->print(flight->origin_country);

        vTaskDelay(pdMS_TO_TICKS(pause_ms));

        // Animate scroll
        for (int frame = 1; frame <= total_frames; frame++) {
            // Callsign
            if (callsign_scrolls && flight->callsign[0] != '\0') {
                int cs_x = -(callsign_scroll_range * frame / total_frames);
                if (cs_x != cs_prev_x) {
                    dma_display->setFont(&FreeSansBold9pt7b);
                    dma_display->setTextSize(1);
                    dma_display->setTextColor(black);
                    dma_display->setCursor(cs_prev_x, callsign_y);
                    dma_display->print(flight->callsign);
                    dma_display->setTextColor(white);
                    dma_display->setCursor(cs_x, callsign_y);
                    dma_display->print(flight->callsign);
                    cs_prev_x = cs_x;
                }
            }

            // Country
            if (country_scrolls) {
                int co_x = -(country_scroll_range * frame / total_frames);
                if (co_x != co_prev_x) {
                    dma_display->setFont(NULL);
                    dma_display->setTextSize(1);
                    dma_display->setTextColor(black);
                    dma_display->setCursor(co_prev_x, country_y);
                    dma_display->print(flight->origin_country);
                    dma_display->setTextColor(cyan);
                    dma_display->setCursor(co_x, country_y);
                    dma_display->print(flight->origin_country);
                    co_prev_x = co_x;
                }
            }

            vTaskDelay(pdMS_TO_TICKS(frame_ms));
        }

        // Pause at end
        vTaskDelay(pdMS_TO_TICKS(pause_ms));
    }
}
