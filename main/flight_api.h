#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define MAX_FLIGHTS 20
#define MAX_CALLSIGN_LEN 9
#define MAX_COUNTRY_LEN 32

typedef struct {
    char callsign[MAX_CALLSIGN_LEN];
    char origin_country[MAX_COUNTRY_LEN];
    float altitude;   // barometric altitude in meters
    float velocity;   // ground speed in m/s
    float heading;    // track angle in degrees
    bool on_ground;   // true if aircraft is on the ground
} flight_t;

typedef struct {
    flight_t flights[MAX_FLIGHTS];
    int count;
} flight_data_t;

// Set OpenSky credentials for authenticated requests (more API credits).
// Pass NULL/empty strings for anonymous access.
void flight_api_set_credentials(const char *username, const char *password);

// Fetch flights within the given bounding box from OpenSky Network.
// Results are written into `data`. Returns ESP_OK on success.
esp_err_t flight_api_fetch(float lamin, float lomin, float lamax, float lomax,
                           flight_data_t *data);
