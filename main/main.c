#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "config_storage.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "flight_api.h"
#include "button.h"
#include "display.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

static const char *TAG = "main";

#define POLL_INTERVAL_MS   20000
#define DISPLAY_CYCLE_MS   8000
#define QUIET_HOUR_START   23    // 11pm
#define QUIET_HOUR_END     6     // 6am
#define SNTP_SYNC_TIMEOUT  15000 // ms

// Shared flight data protected by mutex
static flight_data_t s_flight_data = {0};
static SemaphoreHandle_t s_flight_mutex;
static bool s_time_synced = false;

static void init_sntp(void)
{
    // Set timezone to AEST/AEDT before syncing
    setenv("TZ", "AEST-10AEDT,M10.1.0,M4.1.0/3", 1);
    tzset();

    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    int retry = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET &&
           retry < SNTP_SYNC_TIMEOUT / 500) {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry++;
    }

    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        s_time_synced = true;
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        ESP_LOGI(TAG, "Time synced: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        ESP_LOGW(TAG, "SNTP sync timed out, will retry in background");
    }
}

static bool is_quiet_hours(void)
{
    // Don't enforce quiet hours if time hasn't synced yet
    if (!s_time_synced) {
        // Check if SNTP has synced in the background since startup
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            s_time_synced = true;
        } else {
            return false;
        }
    }

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    int hour = timeinfo.tm_hour;
    return (hour >= QUIET_HOUR_START || hour < QUIET_HOUR_END);
}

// Task: poll OpenSky API periodically
static void flight_poll_task(void *arg)
{
    device_config_t *config = (device_config_t *)arg;
    flight_data_t new_data;

    while (1) {
        if (is_quiet_hours()) {
            ESP_LOGI(TAG, "Quiet hours - sleeping");
            vTaskDelay(pdMS_TO_TICKS(5 * 60 * 1000));
            continue;
        }

        esp_err_t err = flight_api_fetch(config->lamin, config->lomin,
                                         config->lamax, config->lomax, &new_data);
        if (err == ESP_OK) {
            xSemaphoreTake(s_flight_mutex, portMAX_DELAY);
            memcpy(&s_flight_data, &new_data, sizeof(flight_data_t));
            xSemaphoreGive(s_flight_mutex);
            ESP_LOGI(TAG, "Updated: %d airborne flights", new_data.count);
        } else {
            ESP_LOGW(TAG, "Flight fetch failed");
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

// Task: cycle through flights on the display
static void display_task(void *arg)
{
    int current_index = 0;

    while (1) {
        xSemaphoreTake(s_flight_mutex, portMAX_DELAY);
        int count = s_flight_data.count;

        if (count > 0) {
            if (current_index >= count) {
                current_index = 0;
            }
            flight_t flight;
            memcpy(&flight, &s_flight_data.flights[current_index], sizeof(flight_t));
            xSemaphoreGive(s_flight_mutex);

            // animate_flight blocks for DISPLAY_CYCLE_MS (handles scrolling internally)
            display_animate_flight(&flight, current_index, count, DISPLAY_CYCLE_MS);
            current_index++;
        } else {
            xSemaphoreGive(s_flight_mutex);
            display_show_no_flights();
            current_index = 0;
            vTaskDelay(pdMS_TO_TICKS(DISPLAY_CYCLE_MS));
        }
    }
}

void app_main(void)
{
    // Step 1: Arduino HAL must init before anything that touches NVS
    display_arduino_init();

    // Step 2: NVS, config, button
    config_storage_init();
    button_init();

    // Check if button is held on boot for reconfiguration
    bool force_ap = false;
    if (button_is_pressed()) {
        ESP_LOGI(TAG, "Button held, waiting 3 seconds to confirm...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        if (button_is_pressed()) {
            ESP_LOGI(TAG, "Button still held - entering config mode");
            force_ap = true;
        }
    }

    if (force_ap || !config_storage_exists()) {
        if (!force_ap) {
            ESP_LOGI(TAG, "No config found, starting AP for setup");
        }
        wifi_manager_start_ap();
        web_server_start();
        // Start display after WiFi in AP mode too
        display_init();
        display_show_config_mode();  // blocks forever with scrolling animation
        return;
    }

    // Step 3: Connect WiFi BEFORE starting display
    static device_config_t config;
    config_storage_load(&config);

    // Set OpenSky OAuth2 credentials if configured
    if (config.api_client_id[0]) {
        flight_api_set_oauth_credentials(config.api_client_id, config.api_client_secret);
    }

    ESP_LOGI(TAG, "Connecting to WiFi: %s", config.ssid);
    bool wifi_ok = (wifi_manager_start_sta(config.ssid, config.password) == ESP_OK);

    // Step 4: Start display AFTER WiFi
    display_init();

    if (wifi_ok) {
        ESP_LOGI(TAG, "WiFi connected");
        display_show_status("Connected!", config.ssid);
    } else {
        ESP_LOGE(TAG, "WiFi connection failed");
        display_show_status("WiFi FAILED", config.ssid);
        // Stay here - can't do much without WiFi
        return;
    }

    // Brief pause so user can see the connected message
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 5: Validate OpenSky credentials
    if (config.api_client_id[0]) {
        display_show_status("Validating", "credentials...");
        esp_err_t cred_err = flight_api_validate_credentials();
        if (cred_err != ESP_OK) {
            ESP_LOGE(TAG, "OpenSky credentials invalid - clearing config and restarting");
            display_show_status("Bad OpenSky", "credentials!");
            vTaskDelay(pdMS_TO_TICKS(3000));
            // Stop WiFi cleanly before erasing config
            esp_wifi_stop();
            nvs_flash_erase();
            esp_restart();
            return;
        }
    } else {
        ESP_LOGE(TAG, "No OpenSky credentials configured - clearing config");
        display_show_status("No API keys", "configured!");
        vTaskDelay(pdMS_TO_TICKS(3000));
        // Stop WiFi cleanly before erasing config
        esp_wifi_stop();
        nvs_flash_erase();
        esp_restart();
        return;
    }

    // Step 6: Time sync
    init_sntp();

    // Step 6: Start flight polling and display tasks
    s_flight_mutex = xSemaphoreCreateMutex();
    display_show_no_flights();

    xTaskCreate(flight_poll_task, "flight_poll", 8192, &config, 5, NULL);
    xTaskCreate(display_task, "display", 8192, NULL, 4, NULL);
}
