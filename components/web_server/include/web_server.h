#pragma once

#include "esp_http_server.h"

enum class ServerMode {
    AP_MODE,    // Initial setup mode (192.168.4.1)
    STA_MODE    // Settings update mode (on local WiFi network)
};

class WebServer {
public:
    static WebServer& instance();

    void begin(ServerMode mode = ServerMode::AP_MODE);  // Start server with specified mode
    void stop();                                         // Stop server
    bool isRunning();

    // Mode management
    void setMode(ServerMode mode);
    ServerMode getMode() const;

    // Check if reconnection is pending after configuration
    bool shouldReconnect();
    void clearReconnectFlag();

    // Check if flight fetch is pending (from web settings update)
    bool shouldFetchFlights();
    void clearFetchFlightFlag();

private:
    WebServer() = default;
    httpd_handle_t server = nullptr;
    bool reconnect_pending = false;
    bool fetch_flights_pending = false;
    ServerMode current_mode = ServerMode::AP_MODE;

    // HTTP handlers
    static esp_err_t handleRoot(httpd_req_t* req);
    static esp_err_t handleConfigure(httpd_req_t* req);
    static esp_err_t handleDebug(httpd_req_t* req);
};
