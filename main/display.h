#pragma once

#include "flight_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize Arduino HAL - must be called before nvs_flash_init()
void display_arduino_init(void);

// Initialize the HUB75 DMA display - call AFTER WiFi init
void display_init(void);

// Show a test pattern (RGB color bands) to verify wiring
void display_test_pattern(void);

// Show a single flight's info on the display
// index: current flight index (0-based), total: total number of flights
void display_show_flight(const flight_t *flight, int index, int total);

// Show flight with scrolling animation for long callsigns.
// Blocks for duration_ms. Use this from the display task.
void display_animate_flight(const flight_t *flight, int index, int total, int duration_ms);

// Show "No flights" message
void display_show_no_flights(void);

// Show a status message (e.g. "Connecting WiFi...", "AP Mode")
void display_show_status(const char *line1, const char *line2);

// Show config mode screen with scrolling AP name (blocks forever)
void display_show_config_mode(void);

#ifdef __cplusplus
}
#endif
