#pragma once

#include <stdbool.h>
#include "esp_err.h"

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
