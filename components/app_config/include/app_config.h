#pragma once

#include <cstdint>

struct LocationConfig {
    float latitude = 0.0f;
    float longitude = 0.0f;
    bool valid = false;
};

struct TimeConfig {
    char timezone[32] = "AEST-10AEDT,M10.1.0,M4.1.0/3";  // Default: Australia/Sydney
    bool valid = false;
};

struct FlightConfig {
    uint32_t update_interval = 30;   // seconds (30 seconds default - safe for authenticated users: ~2,880 requests/day)
    float bbox_size = 0.5f;          // degrees (~55km radius) - kept for compatibility
    // Bounding box coordinates (lat_min, lat_max, lon_min, lon_max)
    float lat_min = -90.0f;
    float lat_max = 90.0f;
    float lon_min = -180.0f;
    float lon_max = 180.0f;
};

struct OpenSkyAuthConfig {
    char username[64] = "";
    char password[64] = "";
    bool authenticated = false;
};

class AppConfig {
public:
    static AppConfig& instance();

    // Initialization
    void begin();  // Load from NVS

    // Location
    bool hasLocation();
    LocationConfig getLocation();
    void setLocation(float lat, float lon);

    // Time
    TimeConfig getTimeConfig();
    void setTimezone(const char* tz);

    // Flight settings
    FlightConfig getFlightConfig();
    void setFlightUpdateInterval(uint32_t seconds);
    void setBBoxSize(float degrees);
    void setBoundingBox(float lat_min, float lat_max, float lon_min, float lon_max);

    // OpenSky Authentication
    OpenSkyAuthConfig getOpenSkyAuth();
    void setOpenSkyAuth(const char* username, const char* password);
    bool hasOpenSkyAuth();

    // Display
    uint8_t getBrightness();
    void setBrightness(uint8_t value);

    // Validation
    bool isFullyConfigured();  // Returns true if location AND timezone set

private:
    AppConfig() = default;  // Singleton
    void loadFromNVS();
    void saveLocationToNVS();
    void saveTimezoneToNVS();
    void saveFlightConfigToNVS();
    void saveBrightnessToNVS();
    void saveOpenSkyAuthToNVS();

    LocationConfig location;
    TimeConfig timeConfig;
    FlightConfig flightConfig;
    OpenSkyAuthConfig openSkyAuth;
    uint8_t brightness = 128;
};
