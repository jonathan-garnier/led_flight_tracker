#include "web_server.h"
#include "app_config.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "WebServer";

// HTML for the configuration form
static const char* html_page = R"(
<!DOCTYPE html>
<html>
<head>
  <title>LED Flight Tracker Setup</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; margin: 20px; background: #f0f0f0; }
    .container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
    h1 { color: #333; margin-top: 0; }
    input, select { width: 100%; padding: 8px; margin: 8px 0; box-sizing: border-box; border: 1px solid #ddd; border-radius: 4px; }
    button { background: #4CAF50; color: white; padding: 12px; border: none; width: 100%; cursor: pointer; border-radius: 4px; font-size: 16px; }
    button:hover { background: #45a049; }
    .info { background: #e7f3fe; padding: 10px; margin: 10px 0; border-left: 4px solid #2196F3; border-radius: 4px; }
    label { display: block; margin-top: 12px; font-weight: bold; color: #555; }
    .note { font-size: 12px; color: #666; margin-top: 20px; line-height: 1.5; }
  </style>
</head>
<body>
  <div class="container">
    <h1>LED Flight Tracker Setup</h1>
    <div class="info">
      <b>Network:</b> LED-Flight-Tracker<br>
      <b>IP Address:</b> 192.168.4.1
    </div>

    <form method="POST" action="/configure">
      <h2>WiFi Settings</h2>
      <label>Network Name (SSID):</label>
      <input type="text" name="ssid" required maxlength="31" placeholder="Your WiFi network name">

      <label>Password:</label>
      <input type="password" name="password" required maxlength="63" placeholder="Your WiFi password">

      <h2>Location Settings</h2>
      <label>Latitude (decimal degrees):</label>
      <input type="number" step="0.000001" name="latitude" placeholder="e.g., -33.8688" required>

      <label>Longitude (decimal degrees):</label>
      <input type="number" step="0.000001" name="longitude" placeholder="e.g., 151.2093" required>

      <h2>Time Settings</h2>
      <label>Timezone:</label>
      <select name="timezone">
        <option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Australia/Sydney (AEDT)</option>
        <option value="UTC0">UTC</option>
        <option value="PST8PDT,M3.2.0,M11.1.0">US/Pacific</option>
        <option value="EST5EDT,M3.2.0,M11.1.0">US/Eastern</option>
        <option value="CET-1CEST,M3.5.0,M10.5.0/3">Europe/Central</option>
        <option value="JST-9">Asia/Tokyo</option>
      </select>

      <button type="submit">Save & Connect</button>
    </form>

    <p class="note">
      After saving, the device will connect to your WiFi network.<br>
      Find your device's new IP address on your router or check the Info screen.
    </p>
  </div>
</body>
</html>
)";

// Success page
static const char* success_page = R"(
<!DOCTYPE html>
<html>
<head>
  <title>Configuration Saved</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta http-equiv="refresh" content="3;url=/">
  <style>
    body { font-family: Arial; margin: 20px; background: #f0f0f0; text-align: center; padding-top: 50px; }
    .container { max-width: 400px; margin: 0 auto; background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
    h1 { color: #4CAF50; }
    .spinner { border: 4px solid #f3f3f3; border-top: 4px solid #4CAF50; border-radius: 50%; width: 40px; height: 40px; animation: spin 1s linear infinite; margin: 20px auto; }
    @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
  </style>
</head>
<body>
  <div class="container">
    <h1>Configuration Saved!</h1>
    <div class="spinner"></div>
    <p>Connecting to WiFi network...</p>
    <p style="color: #666; font-size: 14px;">This page will close in 3 seconds</p>
  </div>
</body>
</html>
)";

// Singleton instance
WebServer& WebServer::instance() {
    static WebServer inst;
    return inst;
}

// URL decode helper function
static void url_decode(char* dst, const char* src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Parse form data helper
static bool parse_form_value(const char* body, const char* key, char* value, size_t value_size) {
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "%s=", key);

    const char* start = strstr(body, search_key);
    if (!start) return false;

    start += strlen(search_key);
    const char* end = strchr(start, '&');

    size_t len;
    if (end) {
        len = end - start;
    } else {
        len = strlen(start);
    }

    if (len >= value_size) len = value_size - 1;

    char encoded[256];
    strncpy(encoded, start, len);
    encoded[len] = '\0';

    url_decode(value, encoded);
    return true;
}

// GET / - Serve configuration form
esp_err_t WebServer::handleRoot(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, strlen(html_page));
    return ESP_OK;
}

// POST /configure - Handle form submission
esp_err_t WebServer::handleConfigure(httpd_req_t* req) {
    char content[1024];
    size_t recv_size = req->content_len;

    if (recv_size >= sizeof(content)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too large");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    content[recv_size] = '\0';

    ESP_LOGI(TAG, "Received configuration data");

    // Parse form data
    char ssid[32] = {0};
    char password[64] = {0};
    char lat_str[32] = {0};
    char lon_str[32] = {0};
    char timezone[64] = {0};

    if (!parse_form_value(content, "ssid", ssid, sizeof(ssid)) ||
        !parse_form_value(content, "password", password, sizeof(password)) ||
        !parse_form_value(content, "latitude", lat_str, sizeof(lat_str)) ||
        !parse_form_value(content, "longitude", lon_str, sizeof(lon_str)) ||
        !parse_form_value(content, "timezone", timezone, sizeof(timezone))) {

        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing required fields");
        return ESP_FAIL;
    }

    // Validate and convert latitude/longitude
    float lat = atof(lat_str);
    float lon = atof(lon_str);

    if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid latitude or longitude");
        return ESP_FAIL;
    }

    // Validate SSID and password
    if (strlen(ssid) == 0 || strlen(ssid) > 31) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID");
        return ESP_FAIL;
    }

    if (strlen(password) < 8 || strlen(password) > 63) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password must be 8-63 characters");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Configuration valid:");
    ESP_LOGI(TAG, "  SSID: %s", ssid);
    ESP_LOGI(TAG, "  Location: %.4f, %.4f", lat, lon);
    ESP_LOGI(TAG, "  Timezone: %s", timezone);

    // Save to AppConfig
    AppConfig& config = AppConfig::instance();
    config.setLocation(lat, lon);
    config.setTimezone(timezone);

    // Save WiFi credentials
    WiFiManager& wm = WiFiManager::instance();
    wm.saveCredentials(ssid, password);

    // Send success page
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, success_page, strlen(success_page));

    ESP_LOGI(TAG, "Configuration saved, will reconnect WiFi in 2 seconds");

    // Set flag for WiFi reconnection (handled in main loop)
    WebServer::instance().reconnect_pending = true;

    return ESP_OK;
}

// Check if reconnection is pending
bool WebServer::shouldReconnect() {
    return reconnect_pending;
}

// Clear reconnection flag
void WebServer::clearReconnectFlag() {
    reconnect_pending = false;
}

// Start the web server
void WebServer::begin() {
    if (server != nullptr) {
        ESP_LOGW(TAG, "Server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port 80");

    if (httpd_start(&server, &config) == ESP_OK) {
        // Register URI handlers
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = handleRoot,
            .user_ctx = nullptr
        };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t configure_uri = {
            .uri = "/configure",
            .method = HTTP_POST,
            .handler = handleConfigure,
            .user_ctx = nullptr
        };
        httpd_register_uri_handler(server, &configure_uri);

        ESP_LOGI(TAG, "HTTP server started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        server = nullptr;
    }
}

// Stop the web server
void WebServer::stop() {
    if (server != nullptr) {
        ESP_LOGI(TAG, "Stopping HTTP server");
        httpd_stop(server);
        server = nullptr;
    }
}

// Check if server is running
bool WebServer::isRunning() {
    return server != nullptr;
}
