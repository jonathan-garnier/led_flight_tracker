#include "app_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "AppConfig";

// Singleton instance
AppConfig& AppConfig::instance() {
    static AppConfig inst;
    return inst;
}

// ---------------------------------------------------
// Initialization - Load all settings from NVS
// ---------------------------------------------------
void AppConfig::begin() {
    ESP_LOGI(TAG, "Initializing AppConfig...");
    loadFromNVS();

    ESP_LOGI(TAG, "Location: %.4f, %.4f (valid: %s)",
             location.latitude, location.longitude,
             location.valid ? "yes" : "no");
    ESP_LOGI(TAG, "Timezone: %s (valid: %s)",
             timeConfig.timezone,
             timeConfig.valid ? "yes" : "no");
    ESP_LOGI(TAG, "Flight update interval: %lu seconds", flightConfig.update_interval);
    ESP_LOGI(TAG, "Brightness: %d", brightness);
}

// ---------------------------------------------------
// Load all settings from NVS
// ---------------------------------------------------
void AppConfig::loadFromNVS() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("app_config", NVS_READONLY, &handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace 'app_config' not found, using defaults");
        // Set default timezone as valid
        timeConfig.valid = true;
        return;
    }

    // Load location
    float lat, lon;
    size_t lat_size = sizeof(float);
    size_t lon_size = sizeof(float);
    if (nvs_get_blob(handle, "latitude", &lat, &lat_size) == ESP_OK &&
        nvs_get_blob(handle, "longitude", &lon, &lon_size) == ESP_OK) {
        location.latitude = lat;
        location.longitude = lon;
        location.valid = true;
        ESP_LOGI(TAG, "Loaded location from NVS");
    }

    // Load timezone
    size_t tz_len = sizeof(timeConfig.timezone);
    if (nvs_get_str(handle, "timezone", timeConfig.timezone, &tz_len) == ESP_OK) {
        timeConfig.valid = true;
        ESP_LOGI(TAG, "Loaded timezone from NVS");
    } else {
        // Use default timezone
        timeConfig.valid = true;
        ESP_LOGI(TAG, "Using default timezone: %s", timeConfig.timezone);
    }

    // Load flight update interval
    uint32_t interval;
    if (nvs_get_u32(handle, "flight_int", &interval) == ESP_OK) {
        flightConfig.update_interval = interval;
        ESP_LOGI(TAG, "Loaded flight update interval from NVS");
    }

    // Load bounding box size
    float bbox;
    size_t bbox_size = sizeof(float);
    if (nvs_get_blob(handle, "bbox_size", &bbox, &bbox_size) == ESP_OK) {
        flightConfig.bbox_size = bbox;
        ESP_LOGI(TAG, "Loaded bounding box size from NVS");
    }

    // Load bounding box coordinates
    float lat_min_val, lat_max_val, lon_min_val, lon_max_val;
    size_t bbox_coord_size = sizeof(float);
    bool has_valid_bbox = false;

    if (nvs_get_blob(handle, "bbox_lat_min", &lat_min_val, &bbox_coord_size) == ESP_OK &&
        nvs_get_blob(handle, "bbox_lat_max", &lat_max_val, &bbox_coord_size) == ESP_OK &&
        nvs_get_blob(handle, "bbox_lon_min", &lon_min_val, &bbox_coord_size) == ESP_OK &&
        nvs_get_blob(handle, "bbox_lon_max", &lon_max_val, &bbox_coord_size) == ESP_OK) {

        // Validate that we have reasonable bbox values
        if (lat_min_val < lat_max_val && lon_min_val < lon_max_val) {
            flightConfig.lat_min = lat_min_val;
            flightConfig.lat_max = lat_max_val;
            flightConfig.lon_min = lon_min_val;
            flightConfig.lon_max = lon_max_val;
            has_valid_bbox = true;
        }
    }

    // If no valid bbox loaded from NVS, calculate it from location and bbox_size
    if (!has_valid_bbox && location.valid) {
        float half_size = flightConfig.bbox_size / 2.0f;
        flightConfig.lat_min = location.latitude - half_size;
        flightConfig.lat_max = location.latitude + half_size;
        flightConfig.lon_min = location.longitude - half_size;
        flightConfig.lon_max = location.longitude + half_size;
        ESP_LOGI(TAG, "Calculated bbox from location: Lat[%.4f, %.4f], Lon[%.4f, %.4f]",
                 flightConfig.lat_min, flightConfig.lat_max, flightConfig.lon_min, flightConfig.lon_max);
    }

    // Load brightness
    uint8_t bright;
    if (nvs_get_u8(handle, "brightness", &bright) == ESP_OK) {
        brightness = bright;
        ESP_LOGI(TAG, "Loaded brightness from NVS");
    }

    // Load OpenSky authentication
    size_t username_len = sizeof(openSkyAuth.username);
    size_t password_len = sizeof(openSkyAuth.password);
    if (nvs_get_str(handle, "sky_user", openSkyAuth.username, &username_len) == ESP_OK &&
        nvs_get_str(handle, "sky_pass", openSkyAuth.password, &password_len) == ESP_OK &&
        strlen(openSkyAuth.username) > 0) {
        openSkyAuth.authenticated = true;
        ESP_LOGI(TAG, "Loaded OpenSky authentication from NVS");
    }

    nvs_close(handle);
}

// ---------------------------------------------------
// Location Methods
// ---------------------------------------------------
bool AppConfig::hasLocation() {
    return location.valid;
}

LocationConfig AppConfig::getLocation() {
    return location;
}

void AppConfig::setLocation(float lat, float lon) {
    // Validate input
    if (lat < -90.0f || lat > 90.0f) {
        ESP_LOGE(TAG, "Invalid latitude: %.2f (must be -90 to 90)", lat);
        return;
    }

    if (lon < -180.0f || lon > 180.0f) {
        ESP_LOGE(TAG, "Invalid longitude: %.2f (must be -180 to 180)", lon);
        return;
    }

    location.latitude = lat;
    location.longitude = lon;
    location.valid = true;

    // Also update bounding box based on new location and current bbox_size
    float half_size = flightConfig.bbox_size / 2.0f;
    flightConfig.lat_min = lat - half_size;
    flightConfig.lat_max = lat + half_size;
    flightConfig.lon_min = lon - half_size;
    flightConfig.lon_max = lon + half_size;

    saveLocationToNVS();
    saveFlightConfigToNVS();

    ESP_LOGI(TAG, "Location set to: %.4f, %.4f", lat, lon);
    ESP_LOGI(TAG, "Bounding box updated: Lat[%.4f, %.4f], Lon[%.4f, %.4f]",
             flightConfig.lat_min, flightConfig.lat_max, flightConfig.lon_min, flightConfig.lon_max);
}

void AppConfig::saveLocationToNVS() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("app_config", NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing location");
        return;
    }

    nvs_set_blob(handle, "latitude", &location.latitude, sizeof(float));
    nvs_set_blob(handle, "longitude", &location.longitude, sizeof(float));
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Location saved to NVS");
}

// ---------------------------------------------------
// Time Configuration Methods
// ---------------------------------------------------
TimeConfig AppConfig::getTimeConfig() {
    return timeConfig;
}

void AppConfig::setTimezone(const char* tz) {
    if (tz == nullptr || strlen(tz) == 0 || strlen(tz) >= sizeof(timeConfig.timezone)) {
        ESP_LOGE(TAG, "Invalid timezone string");
        return;
    }

    strncpy(timeConfig.timezone, tz, sizeof(timeConfig.timezone) - 1);
    timeConfig.timezone[sizeof(timeConfig.timezone) - 1] = '\0';
    timeConfig.valid = true;

    saveTimezoneToNVS();

    ESP_LOGI(TAG, "Timezone set to: %s", tz);
}

void AppConfig::saveTimezoneToNVS() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("app_config", NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing timezone");
        return;
    }

    nvs_set_str(handle, "timezone", timeConfig.timezone);
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Timezone saved to NVS");
}

// ---------------------------------------------------
// Flight Configuration Methods
// ---------------------------------------------------
FlightConfig AppConfig::getFlightConfig() {
    return flightConfig;
}

void AppConfig::setFlightUpdateInterval(uint32_t seconds) {
    if (seconds < 60) {
        ESP_LOGW(TAG, "Flight update interval too short: %lu (minimum 60s)", seconds);
        seconds = 60;
    }

    flightConfig.update_interval = seconds;
    saveFlightConfigToNVS();

    ESP_LOGI(TAG, "Flight update interval set to: %lu seconds", seconds);
}

void AppConfig::setBBoxSize(float degrees) {
    if (degrees <= 0.0f || degrees > 10.0f) {
        ESP_LOGE(TAG, "Invalid bounding box size: %.2f (must be 0-10 degrees)", degrees);
        return;
    }

    flightConfig.bbox_size = degrees;
    saveFlightConfigToNVS();

    ESP_LOGI(TAG, "Bounding box size set to: %.2f degrees", degrees);
}

void AppConfig::setBoundingBox(float lat_min, float lat_max, float lon_min, float lon_max) {
    // Validate bounding box
    if (lat_min >= lat_max || lon_min >= lon_max) {
        ESP_LOGE(TAG, "Invalid bounding box: lat[%.4f, %.4f], lon[%.4f, %.4f]", lat_min, lat_max, lon_min, lon_max);
        return;
    }

    if (lat_min < -90.0f || lat_max > 90.0f || lon_min < -180.0f || lon_max > 180.0f) {
        ESP_LOGE(TAG, "Bounding box out of valid range");
        return;
    }

    flightConfig.lat_min = lat_min;
    flightConfig.lat_max = lat_max;
    flightConfig.lon_min = lon_min;
    flightConfig.lon_max = lon_max;
    saveFlightConfigToNVS();

    ESP_LOGI(TAG, "Bounding box set to - Lat: [%.4f, %.4f], Lon: [%.4f, %.4f]", lat_min, lat_max, lon_min, lon_max);
}

void AppConfig::saveFlightConfigToNVS() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("app_config", NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing flight config");
        return;
    }

    nvs_set_u32(handle, "flight_int", flightConfig.update_interval);
    nvs_set_blob(handle, "bbox_size", &flightConfig.bbox_size, sizeof(float));
    nvs_set_blob(handle, "bbox_lat_min", &flightConfig.lat_min, sizeof(float));
    nvs_set_blob(handle, "bbox_lat_max", &flightConfig.lat_max, sizeof(float));
    nvs_set_blob(handle, "bbox_lon_min", &flightConfig.lon_min, sizeof(float));
    nvs_set_blob(handle, "bbox_lon_max", &flightConfig.lon_max, sizeof(float));
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Flight config saved to NVS");
}

// ---------------------------------------------------
// Display Methods
// ---------------------------------------------------
uint8_t AppConfig::getBrightness() {
    return brightness;
}

void AppConfig::setBrightness(uint8_t value) {
    brightness = value;
    saveBrightnessToNVS();

    ESP_LOGI(TAG, "Brightness set to: %d", value);
}

void AppConfig::saveBrightnessToNVS() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("app_config", NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing brightness");
        return;
    }

    nvs_set_u8(handle, "brightness", brightness);
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Brightness saved to NVS");
}

// ---------------------------------------------------
// OpenSky Authentication Methods
// ---------------------------------------------------
OpenSkyAuthConfig AppConfig::getOpenSkyAuth() {
    return openSkyAuth;
}

bool AppConfig::hasOpenSkyAuth() {
    return openSkyAuth.authenticated;
}

void AppConfig::setOpenSkyAuth(const char* username, const char* password) {
    if (username == nullptr || password == nullptr) {
        ESP_LOGE(TAG, "Username or password is null");
        return;
    }

    if (strlen(username) == 0 || strlen(password) == 0) {
        ESP_LOGE(TAG, "Username or password is empty");
        return;
    }

    if (strlen(username) >= sizeof(openSkyAuth.username) ||
        strlen(password) >= sizeof(openSkyAuth.password)) {
        ESP_LOGE(TAG, "Username or password too long");
        return;
    }

    strncpy(openSkyAuth.username, username, sizeof(openSkyAuth.username) - 1);
    openSkyAuth.username[sizeof(openSkyAuth.username) - 1] = '\0';

    strncpy(openSkyAuth.password, password, sizeof(openSkyAuth.password) - 1);
    openSkyAuth.password[sizeof(openSkyAuth.password) - 1] = '\0';

    // Mark as NOT authenticated - credentials must be validated by API before enabling 30-second rate limit
    openSkyAuth.authenticated = false;
    saveOpenSkyAuthToNVS();

    ESP_LOGI(TAG, "OpenSky credentials stored for user: %s (pending validation)", username);
}

void AppConfig::saveOpenSkyAuthToNVS() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("app_config", NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing OpenSky auth");
        return;
    }

    nvs_set_str(handle, "sky_user", openSkyAuth.username);
    nvs_set_str(handle, "sky_pass", openSkyAuth.password);
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "OpenSky authentication saved to NVS");
}

void AppConfig::validateOpenSkyAuth() {
    if (strlen(openSkyAuth.username) > 0 && strlen(openSkyAuth.password) > 0) {
        openSkyAuth.authenticated = true;
        saveOpenSkyAuthToNVS();
        ESP_LOGI(TAG, "OpenSky credentials validated - enabling 30-second rate limit");
    }
}

void AppConfig::clearOpenSkyAuth() {
    openSkyAuth.authenticated = false;

    // Clear the stored credentials from NVS so they don't auto-enable on next boot
    nvs_handle_t handle;
    esp_err_t err = nvs_open("app_config", NVS_READWRITE, &handle);

    if (err == ESP_OK) {
        // Clear the stored credentials
        nvs_erase_key(handle, "sky_user");
        nvs_erase_key(handle, "sky_pass");
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGW(TAG, "OpenSky credentials cleared from NVS");
    } else {
        ESP_LOGW(TAG, "Failed to open NVS for clearing credentials");
    }

    ESP_LOGW(TAG, "OpenSky authentication cleared - reverting to unauthenticated rate limit (5 minutes)");
}

// ---------------------------------------------------
// Validation
// ---------------------------------------------------
bool AppConfig::isFullyConfigured() {
    return location.valid && timeConfig.valid;
}
