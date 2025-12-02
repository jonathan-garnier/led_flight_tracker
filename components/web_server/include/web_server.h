#pragma once

#include "esp_http_server.h"

class WebServer {
public:
    static WebServer& instance();

    void begin();    // Start server (call when in AP mode)
    void stop();     // Stop server (call when leaving AP mode)
    bool isRunning();

    // Check if reconnection is pending after configuration
    bool shouldReconnect();
    void clearReconnectFlag();

private:
    WebServer() = default;
    httpd_handle_t server = nullptr;
    bool reconnect_pending = false;

    // HTTP handlers
    static esp_err_t handleRoot(httpd_req_t* req);
    static esp_err_t handleConfigure(httpd_req_t* req);
};
