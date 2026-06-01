#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ROUTE_API_FOUND = 0,     // origin/dest populated
    ROUTE_API_NOT_FOUND,     // callsign not in the route database (HTTP 404)
    ROUTE_API_ERROR,         // network/parse error - retry later
} route_api_result_t;

// Look up the origin/destination airports for a callsign using the free
// adsb.lol vrs-standing-data per-callsign JSON endpoint (no API key).
//
// On ROUTE_API_FOUND, `origin` and `dest` receive airport codes (IATA when
// available, otherwise ICAO; NUL-terminated, buffers must be >= 5 bytes). On
// any other result the buffers are set to empty strings.
route_api_result_t route_api_fetch(const char *callsign, char origin[5], char dest[5]);

#ifdef __cplusplus
}
#endif
