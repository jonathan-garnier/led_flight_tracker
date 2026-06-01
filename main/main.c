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
#include "route_api.h"
#include "route_cache.h"
#include "button.h"
#include "display.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "mdns.h"

static const char *TAG = "main";

#define MDNS_HOSTNAME "flighttracker"

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

// Populate a flight's origin/dest/route_status from the in-RAM route cache.
// Cache hits resolve instantly; misses are marked PENDING for the resolver task.
static void fill_route_from_cache(flight_t *f)
{
    f->origin[0] = '\0';
    f->dest[0] = '\0';
    if (f->callsign[0] == '\0') {
        f->route_status = ROUTE_UNKNOWN;
        return;
    }
    // Helicopters show an animated rotorcraft instead of a route, so don't
    // queue a (meaningless) route lookup for them.
    if (f->category == FLIGHT_CATEGORY_ROTORCRAFT) {
        f->route_status = ROUTE_UNKNOWN;
        return;
    }
    switch (route_cache_lookup(f->callsign, f->origin, f->dest)) {
    case ROUTE_CACHE_FOUND: f->route_status = ROUTE_RESOLVED; break;
    case ROUTE_CACHE_NONE:  f->route_status = ROUTE_NONE;     break;
    default:                f->route_status = ROUTE_PENDING;  break;
    }
}

// Write a resolved route back into every live flight sharing the callsign.
static void apply_route_to_live(const char *callsign, uint8_t status,
                                const char *origin, const char *dest)
{
    xSemaphoreTake(s_flight_mutex, portMAX_DELAY);
    for (int i = 0; i < s_flight_data.count; i++) {
        flight_t *f = &s_flight_data.flights[i];
        if (strncmp(f->callsign, callsign, MAX_CALLSIGN_LEN) != 0) continue;
        f->route_status = status;
        if (status == ROUTE_RESOLVED) {
            strncpy(f->origin, origin, MAX_AIRPORT_LEN - 1);
            f->origin[MAX_AIRPORT_LEN - 1] = '\0';
            strncpy(f->dest, dest, MAX_AIRPORT_LEN - 1);
            f->dest[MAX_AIRPORT_LEN - 1] = '\0';
        } else {
            f->origin[0] = '\0';
            f->dest[0] = '\0';
        }
    }
    xSemaphoreGive(s_flight_mutex);
}

// Task: resolve routes for PENDING callsigns via the route cache / adsb.lol.
// Runs below the display task and throttles its HTTPS calls to be a good
// citizen toward the free data source.
static void route_resolver_task(void *arg)
{
    while (1) {
        // Snapshot the unique PENDING callsigns currently on display.
        char pending[MAX_FLIGHTS][MAX_CALLSIGN_LEN];
        int np = 0;

        xSemaphoreTake(s_flight_mutex, portMAX_DELAY);
        for (int i = 0; i < s_flight_data.count; i++) {
            flight_t *f = &s_flight_data.flights[i];
            if (f->route_status != ROUTE_PENDING || f->callsign[0] == '\0') continue;
            bool dup = false;
            for (int k = 0; k < np; k++) {
                if (strncmp(pending[k], f->callsign, MAX_CALLSIGN_LEN) == 0) { dup = true; break; }
            }
            if (!dup && np < MAX_FLIGHTS) {
                strncpy(pending[np], f->callsign, MAX_CALLSIGN_LEN - 1);
                pending[np][MAX_CALLSIGN_LEN - 1] = '\0';
                np++;
            }
        }
        xSemaphoreGive(s_flight_mutex);

        if (np == 0) {
            route_cache_flush();               // persist anything accumulated
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        bool any_resolved = false;
        for (int k = 0; k < np; k++) {
            char origin[MAX_AIRPORT_LEN] = {0};
            char dest[MAX_AIRPORT_LEN] = {0};
            uint8_t status = ROUTE_PENDING;

            route_cache_status_t cst = route_cache_lookup(pending[k], origin, dest);
            if (cst == ROUTE_CACHE_FOUND) {
                status = ROUTE_RESOLVED;
            } else if (cst == ROUTE_CACHE_NONE) {
                status = ROUTE_NONE;
            } else {
                route_api_result_t r = route_api_fetch(pending[k], origin, dest);
                if (r == ROUTE_API_FOUND) {
                    route_cache_put(pending[k], origin, dest, true);
                    status = ROUTE_RESOLVED;
                } else if (r == ROUTE_API_NOT_FOUND) {
                    route_cache_put(pending[k], NULL, NULL, false);
                    status = ROUTE_NONE;
                } else {
                    status = ROUTE_PENDING;   // network error - retry next pass
                }
                vTaskDelay(pdMS_TO_TICKS(1000));  // throttle network lookups only
            }

            if (status != ROUTE_PENDING) {
                apply_route_to_live(pending[k], status, origin, dest);
                any_resolved = true;
            }
        }

        if (any_resolved) route_cache_flush();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
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
            // Fill routes from the cache (instant); misses become PENDING and
            // are resolved over HTTPS by route_resolver_task.
            for (int i = 0; i < new_data.count; i++) {
                fill_route_from_cache(&new_data.flights[i]);
            }
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
            display_show_idle(DISPLAY_CYCLE_MS);
            current_index = 0;
        }
    }
}

// Called by web server to get current flight count (thread-safe)
int app_get_flight_count(void)
{
    int count = 0;
    if (s_flight_mutex && xSemaphoreTake(s_flight_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        count = s_flight_data.count;
        xSemaphoreGive(s_flight_mutex);
    }
    return count;
}

void app_main(void)
{
    // Step 1: Arduino HAL must init before anything that touches NVS
    display_arduino_init();

    // Step 2: NVS, config, button
    config_storage_init();
    route_cache_init();   // load persisted callsign->route cache from NVS
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

    // Load colour theme from NVS
    color_theme_t theme;
    config_storage_get_color_theme(&theme);
    display_set_color_theme(theme.route, theme.callsign, theme.speed, theme.counter);

    char ip_str[16] = "";
    if (wifi_ok) {
        ESP_LOGI(TAG, "WiFi connected");
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "Settings page: http://%s/settings", ip_str);
        }

        // Advertise a .local hostname via mDNS so the settings page is
        // reachable at http://flighttracker.local/settings
        if (mdns_init() == ESP_OK) {
            mdns_hostname_set(MDNS_HOSTNAME);
            mdns_instance_name_set("Flight Tracker");
            mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
            ESP_LOGI(TAG, "mDNS started: http://%s.local/settings", MDNS_HOSTNAME);
        } else {
            ESP_LOGW(TAG, "mDNS init failed");
        }

        display_show_status("Connected!", config.ssid);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // Show hostname (preferred) and IP so user can access settings page.
        // Scrolls the long text so it doesn't run off the 64px panel.
        display_show_status_scroll("Settings at:", MDNS_HOSTNAME ".local", 8000);
        if (ip_str[0]) {
            display_show_status_scroll("or IP:", ip_str, 6000);
        }
    } else {
        ESP_LOGE(TAG, "WiFi connection failed - will reset to AP mode");
        display_show_status("WiFi FAILED", config.ssid);
        vTaskDelay(pdMS_TO_TICKS(3000));
        display_show_status("Resetting to", "setup mode...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_stop();
        nvs_flash_erase();
        esp_restart();
        return;
    }

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

    // Step 7: Start settings web server
    web_server_start_settings();

    // Step 8: Start flight polling and display tasks
    s_flight_mutex = xSemaphoreCreateMutex();
    display_show_no_flights();

    xTaskCreate(flight_poll_task, "flight_poll", 8192, &config, 5, NULL);
    xTaskCreate(display_task, "display", 8192, NULL, 4, NULL);
    xTaskCreate(route_resolver_task, "route_resolver", 8192, NULL, 3, NULL);
}
