#include "web_server.h"
#include "app_config.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "WebServer";

// HTML for the configuration form
static const char* html_page = "<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"  <title>LED Flight Tracker Setup</title>\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"  <style>\n"
"    body { font-family: Arial; margin: 20px; background: #f0f0f0; }\n"
"    .container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
"    h1 { color: #333; margin-top: 0; }\n"
"    input, select { width: 100%; padding: 8px; margin: 8px 0; box-sizing: border-box; border: 1px solid #ddd; border-radius: 4px; }\n"
"    button { background: #4CAF50; color: white; padding: 12px; border: none; width: 100%; cursor: pointer; border-radius: 4px; font-size: 16px; }\n"
"    button:hover { background: #45a049; }\n"
"    .toggle-btn { background: #2196F3; padding: 6px 12px; font-size: 12px; width: auto; display: inline-block; margin-left: 8px; margin-top: 8px; }\n"
"    .toggle-btn:hover { background: #0b7dda; }\n"
"    .info { background: #e7f3fe; padding: 10px; margin: 10px 0; border-left: 4px solid #2196F3; border-radius: 4px; }\n"
"    label { display: block; margin-top: 12px; font-weight: bold; color: #555; }\n"
"    .label-row { display: flex; justify-content: space-between; align-items: center; }\n"
"    .note { font-size: 12px; color: #666; margin-top: 20px; line-height: 1.5; }\n"
"  </style>\n"
"  <script>\n"
"    function togglePasswordVisibility() {\n"
"      const field = document.getElementById('sky_pass');\n"
"      const btn = document.getElementById('toggle_btn');\n"
"      if (field.type === 'password') {\n"
"        field.type = 'text';\n"
"        btn.textContent = 'Hide';\n"
"      } else {\n"
"        field.type = 'password';\n"
"        btn.textContent = 'Show';\n"
"      }\n"
"    }\n"
"  </script>\n"
"</head>\n"
"<body>\n"
"  <div class=\"container\">\n"
"    <h1>LED Flight Tracker Setup</h1>\n"
"    <div class=\"info\">\n"
"      <b>Network:</b> LED-Flight-Tracker<br>\n"
"      <b>IP Address:</b> 192.168.4.1\n"
"    </div>\n"
"\n"
"    <form method=\"POST\" action=\"/configure\">\n"
"      <h2>WiFi Settings</h2>\n"
"      <label>Network Name (SSID):</label>\n"
"      <input type=\"text\" name=\"ssid\" required maxlength=\"31\" placeholder=\"Your WiFi network name\">\n"
"\n"
"      <label>Password:</label>\n"
"      <input type=\"password\" name=\"password\" required maxlength=\"63\" placeholder=\"Your WiFi password\">\n"
"\n"
"      <h2>Location Settings</h2>\n"
"      <label>Latitude (decimal degrees):</label>\n"
"      <input type=\"number\" step=\"0.000001\" name=\"latitude\" placeholder=\"e.g., -33.8688\" required>\n"
"\n"
"      <label>Longitude (decimal degrees):</label>\n"
"      <input type=\"number\" step=\"0.000001\" name=\"longitude\" placeholder=\"e.g., 151.2093\" required>\n"
"\n"
"      <h2>Time Settings</h2>\n"
"      <label>Timezone:</label>\n"
"      <select name=\"timezone\">\n"
"        <option value=\"AEST-10AEDT,M10.1.0,M4.1.0/3\">Australia/Sydney (AEDT)</option>\n"
"        <option value=\"UTC0\">UTC</option>\n"
"        <option value=\"PST8PDT,M3.2.0,M11.1.0\">US/Pacific</option>\n"
"        <option value=\"EST5EDT,M3.2.0,M11.1.0\">US/Eastern</option>\n"
"        <option value=\"CET-1CEST,M3.5.0,M10.5.0/3\">Europe/Central</option>\n"
"        <option value=\"JST-9\">Asia/Tokyo</option>\n"
"      </select>\n"
"\n"
"      <h2>OpenSky Network (Optional)</h2>\n"
"      <p style=\"color: #666; font-size: 14px;\">\n"
"        To enable departure/arrival airport information, create an API credential in your OpenSky Network account:<br>\n"
"        1. Log in at <b>opensky-network.org</b><br>\n"
"        2. Go to Account Settings and API Credentials<br>\n"
"        3. Create a new credential - you will get a username and password<br>\n"
"        4. Enter those credentials below - not your account username and password\n"
"      </p>\n"
"      <label>OpenSky API Username:</label>\n"
"      <input type=\"text\" name=\"sky_user\" maxlength=\"63\" placeholder=\"Your API credential username (optional)\">\n"
"\n"
"      <div class=\"label-row\">\n"
"        <label>OpenSky API Password:</label>\n"
"        <button type=\"button\" id=\"toggle_btn\" class=\"toggle-btn\" onclick=\"togglePasswordVisibility()\">Show</button>\n"
"      </div>\n"
"      <input type=\"password\" id=\"sky_pass\" name=\"sky_pass\" maxlength=\"63\" placeholder=\"Your API credential password (optional)\">\n"
"\n"
"      <button type=\"submit\">Save &amp; Connect</button>\n"
"    </form>\n"
"\n"
"    <p class=\"note\">\n"
"      After saving, the device will connect to your WiFi network.<br>\n"
"      Find your device IP address on your router or check the Info screen.<br>\n"
"      <br>\n"
"      <b>Note:</b> OpenSky Network API credentials are optional. Without them, the tracker will display flight callsigns and positions. With credentials, you will also see departure and arrival airport codes. Use your API credential username and password, not your account login.\n"
"    </p>\n"
"  </div>\n"
"</body>\n"
"</html>\n";

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
    char sky_user[64] = {0};
    char sky_pass[64] = {0};

    if (!parse_form_value(content, "ssid", ssid, sizeof(ssid)) ||
        !parse_form_value(content, "password", password, sizeof(password)) ||
        !parse_form_value(content, "latitude", lat_str, sizeof(lat_str)) ||
        !parse_form_value(content, "longitude", lon_str, sizeof(lon_str)) ||
        !parse_form_value(content, "timezone", timezone, sizeof(timezone))) {

        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing required fields");
        return ESP_FAIL;
    }

    // OpenSky credentials are optional
    parse_form_value(content, "sky_user", sky_user, sizeof(sky_user));
    parse_form_value(content, "sky_pass", sky_pass, sizeof(sky_pass));

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
    if (strlen(sky_user) > 0) {
        ESP_LOGI(TAG, "  OpenSky user: %s", sky_user);
    }

    // Save to AppConfig
    AppConfig& config = AppConfig::instance();
    config.setLocation(lat, lon);
    config.setTimezone(timezone);

    // Save OpenSky credentials if provided
    if (strlen(sky_user) > 0 && strlen(sky_pass) > 0) {
        config.setOpenSkyAuth(sky_user, sky_pass);
    }

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

// Debug endpoint to show saved credentials
esp_err_t WebServer::handleDebug(httpd_req_t* req) {
    OpenSkyAuthConfig auth = AppConfig::instance().getOpenSkyAuth();

    // Create a response showing the saved credentials (for debugging)
    const char* response_start = "<!DOCTYPE html>\n<html><head><title>Debug Info</title></head><body>\n"
                                  "<h2>Saved Configuration</h2>\n"
                                  "<p><b>OpenSky API Username:</b> ";
    const char* response_end = "</p></body></html>";

    char response[512];
    snprintf(response, sizeof(response),
             "%s%s</p>\n"
             "<p><b>Password Length:</b> %zu characters</p>\n"
             "<p><b>Password (first 5 chars):</b> %.5s***</p>\n"
             "<p><b>Authenticated:</b> %s</p>\n"
             "%s",
             response_start,
             auth.username,
             strlen(auth.password),
             auth.password,
             auth.authenticated ? "Yes" : "No",
             response_end);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, response, strlen(response));
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

        httpd_uri_t debug_uri = {
            .uri = "/debug",
            .method = HTTP_GET,
            .handler = handleDebug,
            .user_ctx = nullptr
        };
        httpd_register_uri_handler(server, &debug_uri);

        ESP_LOGI(TAG, "HTTP server started successfully");
        ESP_LOGI(TAG, "Debug info available at http://192.168.4.1/debug");
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
