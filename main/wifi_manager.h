#pragma once

#include "esp_err.h"

// Start WiFi in AP mode for configuration
esp_err_t wifi_manager_start_ap(void);

// Start WiFi in STA mode and connect to the given network
esp_err_t wifi_manager_start_sta(const char *ssid, const char *password);
