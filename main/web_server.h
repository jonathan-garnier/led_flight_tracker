#pragma once

#include "esp_err.h"

// Start web server in AP config mode (registers / and /config)
esp_err_t web_server_start(void);

// Start web server in STA mode with settings endpoints (/settings)
esp_err_t web_server_start_settings(void);

// Get current flight count (defined in main.c, used by settings page)
int app_get_flight_count(void);
