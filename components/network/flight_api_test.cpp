#include "flight_api.h"
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_crt_bundle.h>
#include <cJSON.h>
#include <string.h>

static const char* TAG = "FlightAPI_Test";

// Buffer for responses
#define MAX_RESPONSE_SIZE (32 * 1024)  // Reduced from 64KB to save heap memory
static char response_buffer[MAX_RESPONSE_SIZE];
static size_t response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA: {
            if (response_len + evt->data_len < MAX_RESPONSE_SIZE) {
                memcpy(response_buffer + response_len, evt->data, evt->data_len);
                response_len += evt->data_len;
            }
            break;
        }
        default:
            break;
    }
    return ESP_OK;
}

// Test OpenSky with larger radius
void test_opensky_large_radius() {
    ESP_LOGI(TAG, "\n=== Testing OpenSky with large radius (2.0 degrees) ===");

    response_len = 0;
    memset(response_buffer, 0, MAX_RESPONSE_SIZE);

    // Sydney: -33.95, 151.18, large 2.0 degree radius
    float lat = -33.95f;
    float lon = 151.18f;
    float radius = 2.0f;

    float lat_min = lat - radius;
    float lat_max = lat + radius;
    float lon_min = lon - radius;
    float lon_max = lon + radius;

    char url[512];
    snprintf(url, sizeof(url),
             "https://opensky-network.org/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
             lat_min, lon_min, lat_max, lon_max);

    ESP_LOGI(TAG, "URL: %s", url);

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.timeout_ms = 10000;
    config.buffer_size = 2048;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Status: %d, Response length: %d bytes", status, response_len);

    response_buffer[response_len] = '\0';

    cJSON *root = cJSON_Parse(response_buffer);
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return;
    }

    cJSON *states = cJSON_GetObjectItem(root, "states");
    if (states && cJSON_IsArray(states)) {
        int count = cJSON_GetArraySize(states);
        ESP_LOGI(TAG, "Found %d flights with large radius", count);
    } else {
        ESP_LOGW(TAG, "No states array or states is null");
    }

    cJSON_Delete(root);
}

// Test FlightRadar24 API (requires API key, but let's try)
void test_flightradar24() {
    ESP_LOGI(TAG, "\n=== Testing FlightRadar24 API ===");

    response_len = 0;
    memset(response_buffer, 0, MAX_RESPONSE_SIZE);

    // FlightRadar24 bounds API (public, no key needed)
    // bounds=lat_min,lat_max,lon_min,lon_max
    char url[512];
    snprintf(url, sizeof(url),
             "https://api.flightradar24.com/common/v1/flight/list.json?bounds=-34.95,-32.95,150.18,152.18");

    ESP_LOGI(TAG, "URL: %s", url);

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.timeout_ms = 10000;
    config.buffer_size = 2048;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Status: %d, Response length: %d bytes", status, response_len);

    response_buffer[response_len] = '\0';
    if (response_len < 500) {
        ESP_LOGI(TAG, "Response: %s", response_buffer);
    }
}

// Test ADS-B Exchange API (another good source)
void test_adsbexchange() {
    ESP_LOGI(TAG, "\n=== Testing ADS-B Exchange API ===");

    response_len = 0;
    memset(response_buffer, 0, MAX_RESPONSE_SIZE);

    // ADS-B Exchange API (public, no key needed)
    char url[512];
    snprintf(url, sizeof(url),
             "https://public-api.adsbexchange.com/api/v2/lat/-33.95/lon/151.18/dist/200");

    ESP_LOGI(TAG, "URL: %s", url);

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.timeout_ms = 10000;
    config.buffer_size = 2048;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Status: %d, Response length: %d bytes", status, response_len);

    response_buffer[response_len] = '\0';
    if (response_len < 500) {
        ESP_LOGI(TAG, "Response: %s", response_buffer);
    }
}

// Public function to run all tests
void flight_api_test_run_all() {
    ESP_LOGI(TAG, "Starting flight API tests for Sydney...");

    test_opensky_large_radius();
    vTaskDelay(pdMS_TO_TICKS(2000));  // Wait between requests

    test_flightradar24();
    vTaskDelay(pdMS_TO_TICKS(2000));

    test_adsbexchange();

    ESP_LOGI(TAG, "Tests complete");
}
