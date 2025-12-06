#include "flight_api.h"
#include "app_config.h"
#include "wifi_manager.h"
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_crt_bundle.h>
#include <cJSON.h>
#include <string.h>
#include <esp_timer.h>

static const char* TAG = "FlightAPI";

// OpenSky Network API endpoint for all states
static const char* OPENSKY_API_URL = "https://opensky-network.org/api/states/all";

// Buffer for HTTP response
// OpenSky API returns ~100 bytes per flight, we expect max ~100 flights, plus JSON overhead
#define MAX_HTTP_RESPONSE_SIZE (32 * 1024)  // 32KB buffer (was 64KB, reducing to save heap)
static char* http_response_buffer = nullptr;
static size_t http_response_len = 0;

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA: {
            if (http_response_buffer == nullptr) {
                ESP_LOGE(TAG, "ERROR: http_response_buffer is NULL!");
                break;
            }
            // Append data to buffer (handle both chunked and non-chunked responses)
            if (http_response_len + evt->data_len < MAX_HTTP_RESPONSE_SIZE) {
                memcpy(http_response_buffer + http_response_len, evt->data, evt->data_len);
                http_response_len += evt->data_len;
            } else {
                ESP_LOGE(TAG, "Buffer overflow! Would exceed %d bytes", MAX_HTTP_RESPONSE_SIZE);
            }
            break;
        }
        default:
            break;
    }
    return ESP_OK;
}

FlightAPI& FlightAPI::instance() {
    static FlightAPI instance;
    return instance;
}

void FlightAPI::begin() {
    if (!initialized) {
        // Allocate response buffer
        if (http_response_buffer == nullptr) {
            http_response_buffer = (char*)malloc(MAX_HTTP_RESPONSE_SIZE);
            if (http_response_buffer == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate HTTP response buffer");
                return;
            }
        }
        initialized = true;
        ESP_LOGI(TAG, "FlightAPI initialized");
    }
}

bool FlightAPI::canFetch() const {
    if (!initialized) return false;

    int64_t now = esp_timer_get_time() / 1000;  // Convert to milliseconds
    return (now - lastFetchTime) >= MIN_FETCH_INTERVAL;
}

int FlightAPI::getSecondsUntilNextFetch() const {
    if (!initialized) return 0;

    int64_t now = esp_timer_get_time() / 1000;  // Convert to milliseconds
    int64_t elapsed = now - lastFetchTime;

    if (elapsed >= MIN_FETCH_INTERVAL) {
        return 0;
    }

    return (MIN_FETCH_INTERVAL - elapsed) / 1000;  // Convert to seconds
}

bool FlightAPI::fetchFlights(float lat, float lon, float radius) {
    if (!initialized) {
        ESP_LOGE(TAG, "FlightAPI not initialized");
        return false;
    }

    // Check if WiFi is connected
    if (WiFiManager::instance().getState() != WiFiState::CONNECTED) {
        ESP_LOGE(TAG, "WiFi not connected");
        return false;
    }

    // Check rate limiting
    if (!canFetch()) {
        ESP_LOGW(TAG, "Rate limit: %d seconds until next fetch", getSecondsUntilNextFetch());
        return false;
    }

    // Calculate bounding box
    float lat_min = lat - radius;
    float lat_max = lat + radius;
    float lon_min = lon - radius;
    float lon_max = lon + radius;

    ESP_LOGI(TAG, "Fetching flights - Center: (%.4f, %.4f), Radius: %.4f", lat, lon, radius);
    ESP_LOGI(TAG, "Bounding box - Lat: [%.4f, %.4f], Lon: [%.4f, %.4f]", lat_min, lat_max, lon_min, lon_max);

    // Build URL with bounding box parameters
    char url[256];
    snprintf(url, sizeof(url),
             "%s?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
             OPENSKY_API_URL, lat_min, lon_min, lat_max, lon_max);

    ESP_LOGI(TAG, "Fetching flights from: %s", url);

    // Reset response buffer
    http_response_len = 0;
    memset(http_response_buffer, 0, MAX_HTTP_RESPONSE_SIZE);

    // Configure HTTP client
    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.timeout_ms = 10000;
    config.buffer_size = 2048;
    config.crt_bundle_attach = esp_crt_bundle_attach;  // Use certificate bundle for HTTPS

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    // Perform GET request
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP request failed with status: %d", status_code);
        return false;
    }

    // Update last fetch time
    lastFetchTime = esp_timer_get_time() / 1000;

    // Null-terminate response
    http_response_buffer[http_response_len] = '\0';

    ESP_LOGD(TAG, "HTTP Response length: %d bytes", http_response_len);

    // Parse JSON response
    cJSON *root = cJSON_Parse(http_response_buffer);
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        ESP_LOGE(TAG, "Buffer is null-terminated: %d", http_response_buffer[http_response_len] == '\0');
        ESP_LOGE(TAG, "First 200 chars: %.200s", http_response_buffer);
        // Try to identify what we got
        if (http_response_len < 100) {
            ESP_LOGE(TAG, "Full response: %s", http_response_buffer);
        }
        return false;
    }

    // Clear previous flights
    flights.clear();

    // Get the "states" array
    cJSON *states = cJSON_GetObjectItem(root, "states");
    if (states == nullptr) {
        ESP_LOGW(TAG, "States field is null in response");
        char *response_str = cJSON_Print(root);
        if (response_str) {
            ESP_LOGI(TAG, "Full API response: %s", response_str);
            free(response_str);
        }
        cJSON_Delete(root);
        return true;  // Not an error, just no flights in the area
    }

    if (!cJSON_IsArray(states)) {
        ESP_LOGE(TAG, "States is not an array!");
        char *response_str = cJSON_Print(root);
        if (response_str) {
            ESP_LOGI(TAG, "Full API response: %s", response_str);
            free(response_str);
        }
        cJSON_Delete(root);
        return false;
    }

    int num_states = cJSON_GetArraySize(states);
    ESP_LOGI(TAG, "States array size: %d flights found", num_states);

    int count = 0;
    cJSON *state = nullptr;
    cJSON_ArrayForEach(state, states) {
        if (!cJSON_IsArray(state)) continue;

        // Parse flight data from array
        // OpenSky API returns: [icao24, callsign, origin_country, time_position, last_contact,
        //                       longitude, latitude, baro_altitude, on_ground, velocity,
        //                       true_track, vertical_rate, sensors, geo_altitude, squawk, spi, position_source]

        Flight flight;

        // Index 1: callsign
        cJSON *callsign = cJSON_GetArrayItem(state, 1);
        if (callsign && cJSON_IsString(callsign) && callsign->valuestring) {
            strncpy(flight.callsign, callsign->valuestring, sizeof(flight.callsign) - 1);
            flight.callsign[sizeof(flight.callsign) - 1] = '\0';
            // Trim whitespace
            for (int i = strlen(flight.callsign) - 1; i >= 0; i--) {
                if (flight.callsign[i] == ' ') flight.callsign[i] = '\0';
                else break;
            }
        }

        // Index 6: latitude
        cJSON *latitude = cJSON_GetArrayItem(state, 6);
        if (latitude && cJSON_IsNumber(latitude)) {
            flight.latitude = latitude->valuedouble;
        } else {
            continue;  // Skip if no position
        }

        // Index 5: longitude
        cJSON *longitude = cJSON_GetArrayItem(state, 5);
        if (longitude && cJSON_IsNumber(longitude)) {
            flight.longitude = longitude->valuedouble;
        } else {
            continue;  // Skip if no position
        }

        // Index 7: baro_altitude (in meters)
        cJSON *altitude = cJSON_GetArrayItem(state, 7);
        if (altitude && cJSON_IsNumber(altitude)) {
            flight.altitude = altitude->valuedouble;
        }

        // Index 9: velocity (in m/s)
        cJSON *velocity = cJSON_GetArrayItem(state, 9);
        if (velocity && cJSON_IsNumber(velocity)) {
            flight.velocity = velocity->valuedouble;
        }

        // Index 10: true_track (heading in degrees)
        cJSON *heading = cJSON_GetArrayItem(state, 10);
        if (heading && cJSON_IsNumber(heading)) {
            flight.heading = heading->valuedouble;
        }

        // Index 4: last_contact (Unix timestamp)
        cJSON *lastContact = cJSON_GetArrayItem(state, 4);
        if (lastContact && cJSON_IsNumber(lastContact)) {
            flight.lastContact = (int64_t)lastContact->valuedouble;
        }

        flight.valid = true;
        flights.push_back(flight);
        count++;
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Successfully fetched %d flights", count);
    return true;
}

const std::vector<Flight>& FlightAPI::getFlights() const {
    return flights;
}

size_t FlightAPI::getFlightCount() const {
    return flights.size();
}
