#include "web_server.h"
#include "app_config.h"
#include "wifi_manager.h"
#include "flight_api.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

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
"    input, select, textarea { width: 100%; padding: 8px; margin: 8px 0; box-sizing: border-box; border: 1px solid #ddd; border-radius: 4px; font-family: Arial, sans-serif; }\n"
"    textarea { min-height: 120px; resize: vertical; }\n"
"    button { background: #4CAF50; color: white; padding: 12px; border: none; width: 100%; cursor: pointer; border-radius: 4px; font-size: 16px; }\n"
"    button:hover { background: #45a049; }\n"
"    .toggle-btn { background: #2196F3; padding: 6px 12px; font-size: 12px; width: auto; display: inline-block; margin-left: 8px; margin-top: 8px; }\n"
"    .toggle-btn:hover { background: #0b7dda; }\n"
"    .info { background: #e7f3fe; padding: 10px; margin: 10px 0; border-left: 4px solid #2196F3; border-radius: 4px; }\n"
"    label { display: block; margin-top: 12px; font-weight: bold; color: #555; }\n"
"    .label-row { display: flex; justify-content: space-between; align-items: center; }\n"
"    .hint { font-size: 12px; color: #666; margin-top: -5px; }\n"
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
"      <label>Bounding Box (GeoJSON):</label>\n"
"      <textarea name=\"geojson\" maxlength=\"2000\" placeholder=\"Paste GeoJSON polygon here (optional). If provided, will override lat/lon.\"></textarea>\n"
"      <p class=\"hint\">Or use geojson.io to draw a box and paste the GeoJSON here</p>\n"
"\n"
"      <label>Search Radius (degrees):</label>\n"
"      <input type=\"number\" step=\"0.1\" min=\"0.1\" max=\"5.0\" name=\"radius\" placeholder=\"e.g., 0.5 (default)\">\n"
"      <p class=\"hint\">Smaller values = fewer flights but less clutter. ~0.5° ≈ 55km</p>\n"
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

// HTML for STA mode settings update form
static const char* html_page_sta = "<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"  <title>LED Flight Tracker - Settings</title>\n"
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
"    .hint { font-size: 12px; color: #666; margin-top: -5px; }\n"
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
"    <h1>LED Flight Tracker - Settings</h1>\n"
"    <div class=\"info\">\n"
"      <b>Mode:</b> WiFi Connected (Settings Update)<br>\n"
"      <b>Note:</b> Device will apply changes and continue tracking flights\n"
"    </div>\n"
"\n"
"    <form method=\"POST\" action=\"/configure\">\n"
"      <h2>Location Settings</h2>\n"
"      <label>Latitude (decimal degrees):</label>\n"
"      <input type=\"number\" step=\"0.000001\" name=\"latitude\" placeholder=\"e.g., -33.8688\" required>\n"
"\n"
"      <label>Longitude (decimal degrees):</label>\n"
"      <input type=\"number\" step=\"0.000001\" name=\"longitude\" placeholder=\"e.g., 151.2093\" required>\n"
"\n"
"      <label>Search Radius (degrees):</label>\n"
"      <input type=\"number\" step=\"0.1\" min=\"0.1\" max=\"5.0\" name=\"radius\" placeholder=\"e.g., 0.5 (default)\">\n"
"      <p class=\"hint\">Smaller values = fewer flights but less clutter. ~0.5° ≈ 55km</p>\n"
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
"        To enable departure/arrival airport information, use your OpenSky Network API credentials:<br>\n"
"        1. Log in at <b>opensky-network.org</b><br>\n"
"        2. Go to Account Settings and API Credentials<br>\n"
"        3. Enter your API credential username and password below\n"
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
"      <button type=\"submit\">Save Settings</button>\n"
"    </form>\n"
"\n"
"    <p class=\"note\">\n"
"      Your settings will be updated and the device will continue normal operation.<br>\n"
"      Flight data will be fetched with the new location on the next update cycle.\n"
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

    // URL-decode directly from the form data into the output buffer
    // to handle large values like GeoJSON without truncation
    const char* src = start;
    const char* src_end = src + len;
    char* dst = value;
    size_t remaining = value_size - 1;

    while (src < src_end && remaining > 0) {
        if ((*src == '%') && (src + 2 < src_end)) {
            char a = src[1];
            char b = src[2];
            if (isxdigit(a) && isxdigit(b)) {
                if (a >= 'a') a -= 'a' - 'A';
                if (a >= 'A') a -= ('A' - 10);
                else a -= '0';
                if (b >= 'a') b -= 'a' - 'A';
                if (b >= 'A') b -= ('A' - 10);
                else b -= '0';
                *dst++ = 16 * a + b;
                src += 3;
                remaining--;
                continue;
            }
        }

        if (*src == '+') {
            *dst++ = ' ';
        } else {
            *dst++ = *src;
        }
        src++;
        remaining--;
    }
    *dst = '\0';

    return true;
}

// Parse GeoJSON polygon to extract bounding box coordinates
// Returns true if successfully parsed, sets lat_min, lat_max, lon_min, lon_max
static bool parse_geojson_polygon(const char* geojson_str, float* lat_min, float* lat_max, float* lon_min, float* lon_max) {
    if (!geojson_str || strlen(geojson_str) == 0) {
        return false;
    }

    float min_lat = 90.0f, max_lat = -90.0f;
    float min_lon = 180.0f, max_lon = -180.0f;
    int coord_count = 0;
    int bracket_count = 0;

    // Simple approach: scan the entire string for [number, number] coordinate pairs
    const char* current = geojson_str;

    while (*current) {
        // Look for opening bracket
        if (*current == '[') {
            bracket_count++;
            const char* bracket_start = current;
            current++;

            // Skip whitespace
            while (*current && (*current == ' ' || *current == '\n' || *current == '\r' || *current == '\t')) {
                current++;
            }

            // Try to parse first number (longitude in GeoJSON)
            char* end = NULL;
            float lon = strtof(current, &end);

            // Check if we successfully parsed a number
            if (end == NULL || end == current) {
                // Not a number, skip this bracket
                current = bracket_start + 1;
                continue;
            }

            current = end;

            // Skip whitespace and comma
            while (*current && (*current == ' ' || *current == '\n' || *current == '\r' || *current == '\t' || *current == ',')) {
                current++;
            }

            // Try to parse second number (latitude in GeoJSON)
            float lat = strtof(current, &end);

            if (end == NULL || end == current) {
                // Not a number, this wasn't a coordinate pair
                current = bracket_start + 1;
                continue;
            }

            current = end;

            // Skip whitespace after second number
            while (*current && (*current == ' ' || *current == '\n' || *current == '\r' || *current == '\t')) {
                current++;
            }

            // Must be followed by closing bracket
            if (*current == ']') {
                // This looks like a valid coordinate pair [lon, lat]
                // Track min/max bounds
                if (lat >= -90.0f && lat <= 90.0f) {  // Valid latitude
                    if (lat < min_lat) min_lat = lat;
                    if (lat > max_lat) max_lat = lat;
                }
                if (lon >= -180.0f && lon <= 180.0f) {  // Valid longitude
                    if (lon < min_lon) min_lon = lon;
                    if (lon > max_lon) max_lon = lon;
                }
                coord_count++;
                current++;
            } else {
                // Not followed by bracket, skip this one
                current = bracket_start + 1;
            }
        } else {
            current++;
        }
    }

    if (coord_count < 4) {  // Need at least 4 coordinates for a valid polygon
        ESP_LOGI(TAG, "GeoJSON: Found %d brackets, %d coordinates (need >= 4)", bracket_count, coord_count);
        return false;
    }

    *lat_min = min_lat;
    *lat_max = max_lat;
    *lon_min = min_lon;
    *lon_max = max_lon;

    ESP_LOGI(TAG, "GeoJSON parsed: %d coords from %d brackets, lat[%.6f, %.6f], lon[%.6f, %.6f]", coord_count, bracket_count, min_lat, max_lat, min_lon, max_lon);
    return true;
}

// GET / - Serve configuration form (adapts to current server mode)
esp_err_t WebServer::handleRoot(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");

    // For STA mode, generate dynamic form with current config values pre-filled
    if (WebServer::instance().getMode() == ServerMode::STA_MODE) {
        AppConfig& config = AppConfig::instance();
        LocationConfig loc = config.getLocation();
        TimeConfig time_cfg = config.getTimeConfig();
        FlightConfig flight_cfg = config.getFlightConfig();
        OpenSkyAuthConfig auth = config.getOpenSkyAuth();

        // Allocate buffer on heap to avoid stack overflow
        // Increased to 5120 to accommodate all form elements
        char* html = (char*)malloc(5120);
        if (!html) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
            return ESP_FAIL;
        }

        int offset = 0;

        // Header and styles
        offset += snprintf(html + offset, 5120 - offset,
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "  <title>LED Flight Tracker - Settings</title>\n"
            "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "  <style>\n"
            "    body { font-family: Arial; margin: 20px; background: #f0f0f0; }\n"
            "    .container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
            "    h1 { color: #333; margin-top: 0; }\n"
            "    input, select, textarea { width: 100%%; padding: 8px; margin: 8px 0; box-sizing: border-box; border: 1px solid #ddd; border-radius: 4px; font-family: Arial, sans-serif; }\n"
            "    textarea { min-height: 120px; resize: vertical; }\n"
            "    button { background: #4CAF50; color: white; padding: 12px; border: none; width: 100%%; cursor: pointer; border-radius: 4px; font-size: 16px; }\n"
            "    button:hover { background: #45a049; }\n"
            "    .toggle-btn { background: #2196F3; padding: 6px 12px; font-size: 12px; width: auto; display: inline-block; margin-left: 8px; margin-top: 8px; }\n"
            "    .toggle-btn:hover { background: #0b7dda; }\n"
            "    .info { background: #e7f3fe; padding: 10px; margin: 10px 0; border-left: 4px solid #2196F3; border-radius: 4px; }\n"
            "    label { display: block; margin-top: 12px; font-weight: bold; color: #555; }\n"
            "    .hint { font-size: 12px; color: #666; margin-top: -5px; }\n"
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
            "    <h1>LED Flight Tracker - Settings</h1>\n"
            "    <div class=\"info\">\n"
            "      <b>Mode:</b> WiFi Connected (Settings Update)<br>\n"
            "      <b>Note:</b> Device will apply changes and continue tracking flights\n"
            "    </div>\n"
            "\n"
            "    <form method=\"POST\" action=\"/configure\">\n"
            "      <h2>Location Settings</h2>\n"
            "      <label>Latitude (decimal degrees):</label>\n"
            "      <input type=\"number\" step=\"0.000001\" name=\"latitude\" value=\"%.6f\" required>\n"
            "\n"
            "      <label>Longitude (decimal degrees):</label>\n"
            "      <input type=\"number\" step=\"0.000001\" name=\"longitude\" value=\"%.6f\" required>\n"
            "\n"
            "      <label>Bounding Box (GeoJSON):</label>\n"
            "      <textarea name=\"geojson\" maxlength=\"2000\" placeholder=\"Paste GeoJSON polygon here (optional). If provided, will override lat/lon.\"></textarea>\n"
            "      <p class=\"hint\">Or use geojson.io to draw a box and paste the GeoJSON here</p>\n"
            "\n"
            "      <label>Search Radius (degrees):</label>\n"
            "      <input type=\"number\" step=\"0.1\" min=\"0.1\" max=\"5.0\" name=\"radius\" value=\"%.1f\">\n"
            "      <p class=\"hint\">Smaller values = fewer flights but less clutter. ~0.5° ≈ 55km</p>\n"
            "\n"
            "      <h2>Time Settings</h2>\n"
            "      <label>Timezone:</label>\n"
            "      <select name=\"timezone\">\n",
            loc.latitude,
            loc.longitude,
            flight_cfg.bbox_size
        );

        // Timezone options - mark current as selected
        const char* timezones[][2] = {
            {"AEST-10AEDT,M10.1.0,M4.1.0/3", "Australia/Sydney (AEDT)"},
            {"UTC0", "UTC"},
            {"PST8PDT,M3.2.0,M11.1.0", "US/Pacific"},
            {"EST5EDT,M3.2.0,M11.1.0", "US/Eastern"},
            {"CET-1CEST,M3.5.0,M10.5.0/3", "Europe/Central"},
            {"JST-9", "Asia/Tokyo"}
        };

        for (int i = 0; i < 6; i++) {
            const char* selected = (strcmp(time_cfg.timezone, timezones[i][0]) == 0) ? " selected" : "";
            offset += snprintf(html + offset, 5120 - offset,
                "        <option value=\"%s\"%s>%s</option>\n",
                timezones[i][0],
                selected,
                timezones[i][1]
            );
        }

        // Rest of form with OpenSky credentials
        offset += snprintf(html + offset, 5120 - offset,
            "      </select>\n"
            "\n"
            "      <h2>OpenSky Network (Optional)</h2>\n"
            "      <p style=\"color: #666; font-size: 14px;\">\n"
            "        To enable departure/arrival airport information, use your OpenSky Network API credentials:<br>\n"
            "        1. Log in at <b>opensky-network.org</b><br>\n"
            "        2. Go to Account Settings and API Credentials<br>\n"
            "        3. Enter your API credential username and password below\n"
            "      </p>\n"
            "      <label>OpenSky API Username:</label>\n"
            "      <input type=\"text\" name=\"sky_user\" maxlength=\"63\" value=\"%s\">\n"
            "\n"
            "      <div style=\"display: flex; justify-content: space-between; align-items: center;\">\n"
            "        <label>OpenSky API Password:</label>\n"
            "        <button type=\"button\" id=\"toggle_btn\" class=\"toggle-btn\" onclick=\"togglePasswordVisibility()\">Show</button>\n"
            "      </div>\n"
            "      <input type=\"password\" id=\"sky_pass\" name=\"sky_pass\" maxlength=\"63\" value=\"%s\">\n"
            "\n"
            "      <button type=\"submit\">Save Settings</button>\n"
            "    </form>\n"
            "\n"
            "    <p style=\"font-size: 12px; color: #666; margin-top: 20px; line-height: 1.5;\">\n"
            "      Your settings will be updated and the device will continue normal operation.<br>\n"
            "      Flight data will be fetched with the new location immediately.\n"
            "    </p>\n"
            "  </div>\n"
            "</body>\n"
            "</html>\n",
            auth.username,
            auth.password
        );

        httpd_resp_send(req, html, strlen(html));
        free(html);
    } else {
        // AP mode: serve static form
        httpd_resp_send(req, html_page, strlen(html_page));
    }

    return ESP_OK;
}

// POST /configure - Handle form submission (both AP and STA modes)
esp_err_t WebServer::handleConfigure(httpd_req_t* req) {
    // Use heap allocation for larger content buffer to handle GeoJSON polygons
    size_t recv_size = req->content_len;

    if (recv_size >= 4096) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Request too large");
        return ESP_FAIL;
    }

    char* content = (char*)malloc(recv_size + 1);
    if (!content) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {
        free(content);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    content[recv_size] = '\0';

    ESP_LOGI(TAG, "Received configuration data (Mode: %s)",
             WebServer::instance().getMode() == ServerMode::AP_MODE ? "AP_MODE" : "STA_MODE");

    // Parse form data - common fields for both modes
    char lat_str[32] = {0};
    char lon_str[32] = {0};
    char timezone[64] = {0};
    char sky_user[64] = {0};
    char sky_pass[64] = {0};

    // Location and timezone are always required
    if (!parse_form_value(content, "latitude", lat_str, sizeof(lat_str)) ||
        !parse_form_value(content, "longitude", lon_str, sizeof(lon_str)) ||
        !parse_form_value(content, "timezone", timezone, sizeof(timezone))) {

        free(content);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing required fields");
        return ESP_FAIL;
    }

    // OpenSky credentials are optional
    parse_form_value(content, "sky_user", sky_user, sizeof(sky_user));
    parse_form_value(content, "sky_pass", sky_pass, sizeof(sky_pass));

    // Parse optional GeoJSON (bounding box) - allocate on heap to avoid stack overflow
    char* geojson = (char*)malloc(2048);
    if (!geojson) {
        free(content);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    geojson[0] = '\0';
    float lat = atof(lat_str);
    float lon = atof(lon_str);
    float radius = 0.5f;  // Default radius

    // Parse optional radius input from form
    char radius_str[32] = {0};
    if (parse_form_value(content, "radius", radius_str, sizeof(radius_str))) {
        float parsed_radius = atof(radius_str);
        if (parsed_radius >= 0.1f && parsed_radius <= 5.0f) {
            radius = parsed_radius;
        }
    }

    parse_form_value(content, "geojson", geojson, 2048);

    // Bounding box coordinates - will be set from GeoJSON or calculated from center+radius
    float bbox_lat_min = lat - radius;
    float bbox_lat_max = lat + radius;
    float bbox_lon_min = lon - radius;
    float bbox_lon_max = lon + radius;

    // If GeoJSON is provided, parse it for bounding box
    if (strlen(geojson) > 0) {
        float lat_min, lat_max, lon_min, lon_max;
        if (parse_geojson_polygon(geojson, &lat_min, &lat_max, &lon_min, &lon_max)) {
            // Use actual bounding box from GeoJSON
            bbox_lat_min = lat_min;
            bbox_lat_max = lat_max;
            bbox_lon_min = lon_min;
            bbox_lon_max = lon_max;

            // Calculate center point for display
            lat = (lat_min + lat_max) / 2.0f;
            lon = (lon_min + lon_max) / 2.0f;

            // Calculate radius as half the maximum dimension
            float lat_diff = (lat_max - lat_min) / 2.0f;
            float lon_diff = (lon_max - lon_min) / 2.0f;
            radius = (lat_diff > lon_diff) ? lat_diff : lon_diff;

            // Ensure radius is at least 0.1 and at most 5.0
            if (radius < 0.1f) radius = 0.1f;
            if (radius > 5.0f) radius = 5.0f;

            ESP_LOGI(TAG, "Using GeoJSON bounding box - Lat: [%.6f, %.6f], Lon: [%.6f, %.6f]", lat_min, lat_max, lon_min, lon_max);
        } else {
            ESP_LOGI(TAG, "GeoJSON parsing failed, using manual coordinates with radius");
        }
    }

    // Validate and convert latitude/longitude
    if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) {
        free(content);
        free(geojson);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid latitude or longitude");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Configuration valid:");
    ESP_LOGI(TAG, "  Location: %.6f, %.6f", lat, lon);
    ESP_LOGI(TAG, "  Timezone: %s", timezone);
    ESP_LOGI(TAG, "  Search radius: %.4f degrees", radius);
    if (strlen(sky_user) > 0) {
        ESP_LOGI(TAG, "  OpenSky user: %s", sky_user);
    }

    // Save to AppConfig
    AppConfig& config = AppConfig::instance();
    config.setLocation(lat, lon);
    config.setTimezone(timezone);
    config.setBBoxSize(radius);
    config.setBoundingBox(bbox_lat_min, bbox_lat_max, bbox_lon_min, bbox_lon_max);

    // Save OpenSky credentials if provided
    if (strlen(sky_user) > 0 && strlen(sky_pass) > 0) {
        config.setOpenSkyAuth(sky_user, sky_pass);
    }

    // Handle AP mode: parse and save WiFi credentials
    if (WebServer::instance().getMode() == ServerMode::AP_MODE) {
        char ssid[32] = {0};
        char password[64] = {0};

        if (!parse_form_value(content, "ssid", ssid, sizeof(ssid)) ||
            !parse_form_value(content, "password", password, sizeof(password))) {

            free(content);
            free(geojson);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing WiFi credentials");
            return ESP_FAIL;
        }

        // Validate SSID and password
        if (strlen(ssid) == 0 || strlen(ssid) > 31) {
            free(content);
            free(geojson);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID");
            return ESP_FAIL;
        }

        if (strlen(password) < 8 || strlen(password) > 63) {
            free(content);
            free(geojson);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password must be 8-63 characters");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "  SSID: %s", ssid);

        // Save WiFi credentials
        WiFiManager& wm = WiFiManager::instance();
        wm.saveCredentials(ssid, password);

        // Set flag for WiFi reconnection (handled in main loop)
        WebServer::instance().reconnect_pending = true;
        ESP_LOGI(TAG, "AP Mode: Configuration saved, will reconnect WiFi");
    } else {
        // STA mode: apply settings and trigger flight fetch in main loop
        ESP_LOGI(TAG, "STA Mode: Settings updated, will fetch flight data in main loop");

        // Reset fetch timer to bypass rate limiting when settings change
        FlightAPI::instance().resetFetchTimer();

        // Set flag for main loop to fetch flights (don't call from HTTP handler to avoid stack overflow)
        WebServer::instance().fetch_flights_pending = true;
    }

    // Send success page with mode-appropriate message
    httpd_resp_set_type(req, "text/html");
    if (WebServer::instance().getMode() == ServerMode::AP_MODE) {
        httpd_resp_send(req, success_page, strlen(success_page));
    } else {
        // For STA mode, send a success message with immediate feedback
        const char* sta_success = "<!DOCTYPE html>\n<html><head>\n"
            "<title>Settings Updated</title>\n"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "<meta http-equiv=\"refresh\" content=\"3;url=/\">\n"
            "<style>\n"
            "body { font-family: Arial; margin: 20px; background: #f0f0f0; text-align: center; padding-top: 50px; }\n"
            ".container { max-width: 400px; margin: 0 auto; background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
            "h1 { color: #4CAF50; }\n"
            ".spinner { border: 4px solid #f3f3f3; border-top: 4px solid #4CAF50; border-radius: 50%; width: 40px; height: 40px; animation: spin 1s linear infinite; margin: 20px auto; }\n"
            "@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }\n"
            "</style>\n"
            "</head><body>\n"
            "<div class=\"container\">\n"
            "<h1>Settings Updated!</h1>\n"
            "<div class=\"spinner\"></div>\n"
            "<p>Your changes have been saved.</p>\n"
            "<p><b>Fetching flight data with new settings...</b></p>\n"
            "<p style=\"color: #666; font-size: 14px;\">The display will update in a moment. This page will refresh in 3 seconds</p>\n"
            "</div>\n"
            "</body></html>";
        httpd_resp_send(req, sta_success, strlen(sta_success));
    }

    free(content);
    free(geojson);
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

// Check if flight fetch is pending
bool WebServer::shouldFetchFlights() {
    return fetch_flights_pending;
}

// Clear flight fetch flag
void WebServer::clearFetchFlightFlag() {
    fetch_flights_pending = false;
}

// Start the web server
void WebServer::begin(ServerMode mode) {
    if (server != nullptr) {
        ESP_LOGW(TAG, "Server already running");
        return;
    }

    // Set the mode before starting
    current_mode = mode;
    const char* mode_str = (mode == ServerMode::AP_MODE) ? "AP_MODE" : "STA_MODE";
    ESP_LOGI(TAG, "Starting HTTP server in %s", mode_str);

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

        ESP_LOGI(TAG, "HTTP server started successfully in %s", mode_str);
        if (mode == ServerMode::AP_MODE) {
            ESP_LOGI(TAG, "Access form at http://192.168.4.1");
            ESP_LOGI(TAG, "Debug info available at http://192.168.4.1/debug");
        } else {
            ESP_LOGI(TAG, "Settings update server running on local network");
        }
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

// Set server mode
void WebServer::setMode(ServerMode mode) {
    current_mode = mode;
}

// Get server mode
ServerMode WebServer::getMode() const {
    return current_mode;
}
