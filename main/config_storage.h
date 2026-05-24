#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SSID_LEN 33
#define MAX_PASS_LEN 65
#define MAX_API_CLIENT_ID_LEN 65
#define MAX_API_CLIENT_SECRET_LEN 65

typedef struct {
    char ssid[MAX_SSID_LEN];
    char password[MAX_PASS_LEN];
    float lamin;
    float lomin;
    float lamax;
    float lomax;
    char api_client_id[MAX_API_CLIENT_ID_LEN];
    char api_client_secret[MAX_API_CLIENT_SECRET_LEN];
} device_config_t;

esp_err_t config_storage_init(void);
esp_err_t config_storage_save(const device_config_t *config);
esp_err_t config_storage_load(device_config_t *config);
bool config_storage_exists(void);

// Brightness stored as separate NVS key (0-255, default 90)
esp_err_t config_storage_set_brightness(uint8_t brightness);
esp_err_t config_storage_get_brightness(uint8_t *brightness);

// Colour theme: packed RGB values (0xRRGGBB)
typedef struct {
    uint32_t callsign;   // default 0xFFFFFF (white)
    uint32_t country;    // default 0x00FFFF (cyan)
    uint32_t speed;      // default 0xFFFF00 (yellow)
    uint32_t counter;    // default 0xFF00C8 (magenta)
} color_theme_t;

esp_err_t config_storage_set_color_theme(const color_theme_t *theme);
esp_err_t config_storage_get_color_theme(color_theme_t *theme);

#ifdef __cplusplus
}
#endif
