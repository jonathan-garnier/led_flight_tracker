#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MAX_FLIGHTS 20
#define MAX_CALLSIGN_LEN 9
#define MAX_COUNTRY_LEN 32
#define MAX_AIRPORT_LEN 5   // up to 4-letter ICAO code + NUL (IATA is 3)

// Route resolution state for a flight's origin/destination lookup.
typedef enum {
    ROUTE_UNKNOWN = 0,  // no callsign / not looked up
    ROUTE_PENDING,      // queued for background resolution
    ROUTE_RESOLVED,     // origin/dest populated
    ROUTE_NONE,         // looked up but no route in the database
} route_status_t;

typedef struct {
    char callsign[MAX_CALLSIGN_LEN];
    char origin_country[MAX_COUNTRY_LEN];
    float altitude;   // barometric altitude in meters
    float velocity;   // ground speed in m/s
    float heading;    // track angle in degrees
    bool on_ground;   // true if aircraft is on the ground
    char origin[MAX_AIRPORT_LEN];   // origin airport code (IATA preferred), "" if unknown
    char dest[MAX_AIRPORT_LEN];     // destination airport code (IATA preferred), "" if unknown
    uint8_t route_status;           // route_status_t
} flight_t;

typedef struct {
    flight_t flights[MAX_FLIGHTS];
    int count;
} flight_data_t;

// Set OpenSky OAuth2 client credentials for authenticated requests.
// These are used to obtain a Bearer token from OpenSky's auth server.
void flight_api_set_oauth_credentials(const char *client_id, const char *client_secret);

// Validate OAuth2 credentials by obtaining a token and making a test API call.
// Returns ESP_OK if authenticated (x-rate-limit-remaining > 400).
// Returns ESP_ERR_INVALID_ARG if credentials are rejected.
// Returns ESP_FAIL on network or other errors.
esp_err_t flight_api_validate_credentials(void);

// Fetch flights within the given bounding box from OpenSky Network.
// Results are written into `data`. Returns ESP_OK on success.
esp_err_t flight_api_fetch(float lamin, float lomin, float lamax, float lomax,
                           flight_data_t *data);

// Get the last known API credits remaining (-1 if not yet fetched)
int flight_api_get_credits_remaining(void);
