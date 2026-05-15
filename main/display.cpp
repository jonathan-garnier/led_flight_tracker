// Based on official 1_SimpleTestShapes example from
// https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA
//
// Adapted for ESP-IDF with custom pin mapping for 64x32 panel

#include "display.h"
#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include "Arduino.h"
#include "esp_log.h"
#include <Fonts/TomThumb.h>

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

// Helper to calculate centered X position for TomThumb font
// TomThumb: size 1 = ~4px per char (3+1), size 2 = ~8px per char (6+2 with spacing)
static int center_x(const char *text, int char_width, int display_width)
{
    int total = strlen(text) * char_width;
    int x = (display_width - total) / 2;
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
        // TomThumb char widths: size 3 = ~10px, size 2 = ~7px
        int callsign_len = strlen(flight->callsign);
        if (callsign_len <= 6) {
            dma_display->setTextSize(3);
            int callsign_x = center_x(flight->callsign, 10, 64);
            dma_display->setCursor(callsign_x, 16);
        } else {
            dma_display->setTextSize(2);
            int callsign_x = center_x(flight->callsign, 7, 64);
            dma_display->setCursor(callsign_x, 12);
        }
        dma_display->setTextColor(white);
        dma_display->print(flight->callsign);
    }

    // Switch to default font for remaining rows
    dma_display->setFont(NULL);
    dma_display->setTextSize(1);

    // Row 2 (y=19): Country centered
    int country_len = strlen(flight->origin_country);
    int country_x = (64 - country_len * 6) / 2;
    if (country_x < 0) country_x = 0;
    dma_display->setCursor(country_x, 17);
    dma_display->setTextColor(cyan);
    dma_display->print(flight->origin_country);

    // Row 3 (y=27): Speed left, counter right
    int kmh = (int)(flight->velocity * 3.6f + 0.5f);
    char spd_str[16];
    snprintf(spd_str, sizeof(spd_str), "%dkm/h", kmh);
    dma_display->setCursor(1, 25);
    dma_display->setTextColor(yellow);
    dma_display->print(spd_str);

    char counter[24];
    snprintf(counter, sizeof(counter), "%d/%d", index + 1, total);
    int counter_x = 64 - (int)(strlen(counter) * 6);
    dma_display->setCursor(counter_x, 25);
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
