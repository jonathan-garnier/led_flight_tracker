#pragma once

#include <vector>
#include <string>
#include <cstdint>

// Structure to hold flight data
struct Flight {
    char callsign[16];      // Aircraft callsign
    float latitude;         // Current latitude
    float longitude;        // Current longitude
    float altitude;         // Altitude in meters
    float velocity;         // Ground speed in m/s
    float heading;          // Track angle in degrees (0-360)
    int64_t lastContact;    // Unix timestamp of last position update
    bool valid;             // Whether this flight data is valid

    Flight() : latitude(0), longitude(0), altitude(0), velocity(0),
               heading(0), lastContact(0), valid(false) {
        callsign[0] = '\0';
    }
};

// FlightAPI singleton class for fetching flight data from OpenSky Network
class FlightAPI {
public:
    static FlightAPI& instance();

    // Initialize the API client
    void begin();

    // Fetch flights within a bounding box around the given location
    // radius is in degrees (approximately 1 degree = 111km)
    bool fetchFlights(float lat, float lon, float radius);

    // Get the list of flights from the last successful fetch
    const std::vector<Flight>& getFlights() const;

    // Get the number of flights from last fetch
    size_t getFlightCount() const;

    // Check if we're ready to fetch (respects rate limiting)
    bool canFetch() const;

    // Get time until next fetch is allowed (in seconds)
    int getSecondsUntilNextFetch() const;

private:
    FlightAPI() = default;
    FlightAPI(const FlightAPI&) = delete;
    FlightAPI& operator=(const FlightAPI&) = delete;

    std::vector<Flight> flights;
    int64_t lastFetchTime = -999999999;  // Initialize to far past to allow first fetch immediately
    const int MIN_FETCH_INTERVAL = 300000;  // 5 minutes in milliseconds (300 seconds)
    bool initialized = false;
};
