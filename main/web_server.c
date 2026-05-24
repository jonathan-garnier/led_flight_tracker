#include "web_server.h"
#include "config_storage.h"
#include "display.h"
#include "flight_api.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "web_server";

static const char CONFIG_PAGE_HTML[] =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Flight Tracker Setup</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:480px;margin:20px auto;padding:0 16px;background:#1a1a2e;color:#e0e0e0}"
    "h1{color:#00d4ff;text-align:center}"
    "label{display:block;margin-top:14px;font-weight:bold;color:#aaa}"
    "input,textarea{width:100%;padding:10px;margin-top:4px;border:1px solid #333;border-radius:6px;"
    "background:#16213e;color:#e0e0e0;box-sizing:border-box;font-size:14px}"
    "textarea{height:160px;font-family:monospace;font-size:12px}"
    "button{width:100%;padding:14px;margin-top:20px;background:#00d4ff;color:#1a1a2e;border:none;"
    "border-radius:6px;font-size:16px;font-weight:bold;cursor:pointer}"
    "button:hover{background:#00b8d9}"
    ".info{font-size:12px;color:#666;margin-top:4px}"
    "</style></head><body>"
    "<h1>Flight Tracker</h1>"
    "<form method='POST' action='/config'>"
    "<label>WiFi Network Name (SSID)</label>"
    "<input name='ssid' required maxlength='32'>"
    "<label>WiFi Password</label>"
    "<input name='password' type='password' required maxlength='64'>"
    "<label>OpenSky Client ID</label>"
    "<input name='api_cid' required maxlength='64' placeholder='yourname-api-client'>"
    "<label>OpenSky Client Secret</label>"
    "<input name='api_csec' type='password' required maxlength='64'>"
    "<p class='info'>Create a free account at opensky-network.org, then generate API client credentials in your account settings.</p>"
    "<label>GeoJSON Bounding Box</label>"
    "<textarea name='geojson' required placeholder='Paste GeoJSON from geojson.io here...'></textarea>"
    "<p class='info'>Go to geojson.io, draw a rectangle over your area, and paste the JSON output above.</p>"
    "<button type='submit'>Save &amp; Connect</button>"
    "</form></body></html>";

static const char SUCCESS_HTML[] =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Success</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:480px;margin:40px auto;padding:0 16px;background:#1a1a2e;color:#e0e0e0;text-align:center}"
    "h1{color:#00ff88}"
    "</style></head><body>"
    "<h1>Configuration Saved!</h1>"
    "<p>The device will now restart, connect to WiFi, and validate your OpenSky credentials.</p>"
    "<p>You can close this page.</p>"
    "</body></html>";

static bool parse_geojson_bbox(const char *json_str, float *lamin, float *lomin, float *lamax, float *lomax)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return false;

    // geojson.io outputs a FeatureCollection with a Polygon feature for rectangles.
    // Extract the coordinates array and compute the bounding box from the vertices.
    bool ok = false;

    cJSON *features = cJSON_GetObjectItem(root, "features");
    if (!cJSON_IsArray(features) || cJSON_GetArraySize(features) == 0) goto done;

    cJSON *feature = cJSON_GetArrayItem(features, 0);
    cJSON *geometry = cJSON_GetObjectItem(feature, "geometry");
    if (!geometry) goto done;

    cJSON *coords = cJSON_GetObjectItem(geometry, "coordinates");
    if (!cJSON_IsArray(coords)) goto done;

    // For a Polygon, coordinates is [[ring]], where ring is [[lon,lat], ...]
    cJSON *ring = cJSON_GetArrayItem(coords, 0);
    if (!cJSON_IsArray(ring) || cJSON_GetArraySize(ring) < 3) goto done;

    float min_lat = 90, max_lat = -90, min_lon = 180, max_lon = -180;
    int n = cJSON_GetArraySize(ring);
    for (int i = 0; i < n; i++) {
        cJSON *point = cJSON_GetArrayItem(ring, i);
        if (!cJSON_IsArray(point) || cJSON_GetArraySize(point) < 2) continue;
        float lon = (float)cJSON_GetArrayItem(point, 0)->valuedouble;
        float lat = (float)cJSON_GetArrayItem(point, 1)->valuedouble;
        if (lat < min_lat) min_lat = lat;
        if (lat > max_lat) max_lat = lat;
        if (lon < min_lon) min_lon = lon;
        if (lon > max_lon) max_lon = lon;
    }

    if (min_lat < max_lat && min_lon < max_lon) {
        *lamin = min_lat;
        *lamax = max_lat;
        *lomin = min_lon;
        *lomax = max_lon;
        ok = true;
        ESP_LOGI(TAG, "Parsed bbox: [%.4f,%.4f,%.4f,%.4f]", min_lat, min_lon, max_lat, max_lon);
    }

done:
    cJSON_Delete(root);
    return ok;
}

// URL-decode a string in place
static void url_decode(char *str)
{
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], 0};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Find a form field value in URL-encoded body. Returns malloc'd string or NULL.
static char *get_form_field(const char *body, const char *field)
{
    char key[64];
    snprintf(key, sizeof(key), "%s=", field);
    const char *start = strstr(body, key);
    if (!start) return NULL;
    start += strlen(key);

    const char *end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);

    char *value = malloc(len + 1);
    if (!value) return NULL;
    memcpy(value, start, len);
    value[len] = '\0';
    url_decode(value);
    return value;
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, CONFIG_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';

    char *ssid = get_form_field(buf, "ssid");
    char *password = get_form_field(buf, "password");
    char *api_cid = get_form_field(buf, "api_cid");
    char *api_csec = get_form_field(buf, "api_csec");
    char *geojson = get_form_field(buf, "geojson");

    if (!ssid || !password || !api_cid || !api_csec || !geojson) {
        ESP_LOGE(TAG, "Missing form fields");
        free(ssid); free(password); free(api_cid); free(api_csec); free(geojson); free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing fields");
        return ESP_FAIL;
    }

    device_config_t config = {0};
    strncpy(config.ssid, ssid, MAX_SSID_LEN - 1);
    strncpy(config.password, password, MAX_PASS_LEN - 1);
    strncpy(config.api_client_id, api_cid, MAX_API_CLIENT_ID_LEN - 1);
    strncpy(config.api_client_secret, api_csec, MAX_API_CLIENT_SECRET_LEN - 1);

    bool bbox_ok = parse_geojson_bbox(geojson, &config.lamin, &config.lomin, &config.lamax, &config.lomax);

    free(ssid);
    free(password);
    free(api_cid);
    free(api_csec);
    free(geojson);
    free(buf);

    if (!bbox_ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid GeoJSON - could not extract bounding box");
        return ESP_FAIL;
    }

    // Save config and reboot - credentials will be validated on boot
    config_storage_save(&config);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SUCCESS_HTML, HTTPD_RESP_USE_STRLEN);

    // Reboot after a short delay to let the response be sent
    ESP_LOGI(TAG, "Config saved, rebooting in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    httpd_handle_t server = NULL;

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ret;
    }

    httpd_uri_t get_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = config_get_handler,
    };
    httpd_register_uri_handler(server, &get_uri);

    httpd_uri_t post_uri = {
        .uri = "/config",
        .method = HTTP_POST,
        .handler = config_post_handler,
    };
    httpd_register_uri_handler(server, &post_uri);

    ESP_LOGI(TAG, "Config web server started on port %d", config.server_port);
    return ESP_OK;
}

// --- Settings page (STA mode) ---

static const char SETTINGS_HEAD[] =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Flight Tracker Settings</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:480px;margin:20px auto;padding:0 16px;background:#1a1a2e;color:#e0e0e0}"
    "h1{color:#00d4ff;text-align:center}"
    "label{display:block;margin-top:14px;font-weight:bold;color:#aaa}"
    "input[type=range]{width:100%;margin-top:8px;accent-color:#00d4ff}"
    ".val{color:#00d4ff;font-size:24px;text-align:center;display:block;margin:8px 0}"
    "button{width:100%;padding:14px;margin-top:20px;background:#00d4ff;color:#1a1a2e;border:none;"
    "border-radius:6px;font-size:16px;font-weight:bold;cursor:pointer}"
    "button:hover{background:#00b8d9}"
    ".info{font-size:12px;color:#666;margin-top:4px}"
    ".info-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;background:#16213e;"
    "border-radius:6px;padding:12px;margin-top:10px}"
    ".info-grid .lbl{color:#666;font-size:11px;text-transform:uppercase}"
    ".info-grid .val2{color:#e0e0e0;font-size:14px}"
    ".color-row{display:flex;align-items:center;gap:10px;margin-top:8px}"
    ".color-row label{flex:1;margin:0}"
    "input[type=color]{width:60px;height:36px;border:1px solid #333;border-radius:4px;"
    "background:#16213e;cursor:pointer;padding:2px}"
    ".btn-danger{background:#ff4444;color:#fff}"
    ".btn-danger:hover{background:#cc3333}"
    "hr{border:none;border-top:1px solid #333;margin:30px 0}"
    "</style></head><body>"
    "<h1>Flight Tracker</h1>";

static const char SETTINGS_FOOT[] =
    "</body></html>";

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    char buf[512];
    int len;

    httpd_resp_set_type(req, "text/html");

    // Head + CSS
    httpd_resp_send_chunk(req, SETTINGS_HEAD, HTTPD_RESP_USE_STRLEN);

    // --- Device Info ---
    char ip_str[16] = "unknown";
    char ssid_str[33] = "unknown";
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    }
    device_config_t cfg = {0};
    config_storage_load(&cfg);
    strncpy(ssid_str, cfg.ssid, sizeof(ssid_str) - 1);

    int64_t uptime_us = esp_timer_get_time();
    int uptime_min = (int)(uptime_us / 1000000 / 60);
    int up_h = uptime_min / 60;
    int up_m = uptime_min % 60;

    int credits = flight_api_get_credits_remaining();
    int flights = app_get_flight_count();

    char cred_str[16];
    if (credits >= 0) {
        snprintf(cred_str, sizeof(cred_str), "%d", credits);
    } else {
        strcpy(cred_str, "waiting...");
    }

    len = snprintf(buf, sizeof(buf),
        "<div class='info-grid'>"
        "<div><span class='lbl'>WiFi</span><br><span class='val2'>%s</span></div>"
        "<div><span class='lbl'>IP Address</span><br><span class='val2'>%s</span></div>"
        "<div><span class='lbl'>API Credits</span><br><span class='val2'>%s</span></div>"
        "<div><span class='lbl'>Uptime</span><br><span class='val2'>%dh %dm</span></div>"
        "<div><span class='lbl'>Flights Overhead</span><br><span class='val2'>%d</span></div>"
        "</div>",
        ssid_str, ip_str, cred_str, up_h, up_m, flights);
    httpd_resp_send_chunk(req, buf, len);

    // --- Brightness + Colours form ---
    uint8_t brightness = 90;
    config_storage_get_brightness(&brightness);
    color_theme_t theme;
    config_storage_get_color_theme(&theme);

    len = snprintf(buf, sizeof(buf),
        "<form method='POST' action='/settings'>"
        "<label>Brightness</label>"
        "<input type='range' name='brightness' min='1' max='255' value='%d' "
        "oninput=\"document.getElementById('bval').textContent=this.value\">"
        "<span class='val' id='bval'>%d</span>"
        "<p class='info'>1 = dimmest, 255 = brightest</p>",
        brightness, brightness);
    httpd_resp_send_chunk(req, buf, len);

    len = snprintf(buf, sizeof(buf),
        "<label>Colours</label>"
        "<div class='color-row'><label>Callsign</label>"
        "<input type='color' name='clr_call' value='#%06x'></div>"
        "<div class='color-row'><label>Country</label>"
        "<input type='color' name='clr_ctry' value='#%06x'></div>"
        "<div class='color-row'><label>Speed</label>"
        "<input type='color' name='clr_spd' value='#%06x'></div>"
        "<div class='color-row'><label>Counter</label>"
        "<input type='color' name='clr_ctr' value='#%06x'></div>",
        (unsigned)(theme.callsign & 0xFFFFFF),
        (unsigned)(theme.country & 0xFFFFFF),
        (unsigned)(theme.speed & 0xFFFFFF),
        (unsigned)(theme.counter & 0xFFFFFF));
    httpd_resp_send_chunk(req, buf, len);

    httpd_resp_send_chunk(req, "<button type='submit'>Apply</button></form>", HTTPD_RESP_USE_STRLEN);

    // --- Reset button ---
    httpd_resp_send_chunk(req,
        "<hr>"
        "<form method='POST' action='/reset' "
        "onsubmit=\"return confirm('Erase all settings and reboot into setup mode?')\">"
        "<button type='submit' class='btn-danger'>Reset Device</button>"
        "</form>",
        HTTPD_RESP_USE_STRLEN);

    // Footer
    httpd_resp_send_chunk(req, SETTINGS_FOOT, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t settings_post_handler(httpd_req_t *req)
{
    char buf[256];
    int received = 0;
    int to_read = req->content_len < (int)sizeof(buf) - 1 ? req->content_len : (int)sizeof(buf) - 1;

    while (received < to_read) {
        int ret = httpd_req_recv(req, buf + received, to_read - received);
        if (ret <= 0) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';

    // Brightness
    char *val = get_form_field(buf, "brightness");
    if (val) {
        int b = atoi(val);
        if (b < 1) b = 1;
        if (b > 255) b = 255;
        display_set_brightness((uint8_t)b);
        config_storage_set_brightness((uint8_t)b);
        free(val);
    }

    // Colours
    color_theme_t theme;
    config_storage_get_color_theme(&theme);  // start with current

    char *v;
    v = get_form_field(buf, "clr_call");
    if (v) { theme.callsign = (uint32_t)strtol(v + 1, NULL, 16); free(v); }
    v = get_form_field(buf, "clr_ctry");
    if (v) { theme.country = (uint32_t)strtol(v + 1, NULL, 16); free(v); }
    v = get_form_field(buf, "clr_spd");
    if (v) { theme.speed = (uint32_t)strtol(v + 1, NULL, 16); free(v); }
    v = get_form_field(buf, "clr_ctr");
    if (v) { theme.counter = (uint32_t)strtol(v + 1, NULL, 16); free(v); }

    config_storage_set_color_theme(&theme);
    display_set_color_theme(theme.callsign, theme.country, theme.speed, theme.counter);

    // Redirect back to settings page
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/settings");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t reset_post_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<!DOCTYPE html><html><head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:sans-serif;max-width:480px;margin:40px auto;padding:0 16px;"
        "background:#1a1a2e;color:#e0e0e0;text-align:center}"
        "h1{color:#ff4444}</style></head><body>"
        "<h1>Device Reset</h1>"
        "<p>Configuration erased. Rebooting into setup mode...</p>"
        "</body></html>", HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_wifi_stop();
    nvs_flash_erase();
    esp_restart();
    return ESP_OK;
}

static esp_err_t root_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/settings");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t web_server_start_settings(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 8;
    httpd_handle_t server = NULL;

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start settings HTTP server");
        return ret;
    }

    httpd_uri_t settings_get = {
        .uri = "/settings",
        .method = HTTP_GET,
        .handler = settings_get_handler,
    };
    httpd_register_uri_handler(server, &settings_get);

    httpd_uri_t settings_post = {
        .uri = "/settings",
        .method = HTTP_POST,
        .handler = settings_post_handler,
    };
    httpd_register_uri_handler(server, &settings_post);

    httpd_uri_t reset = {
        .uri = "/reset",
        .method = HTTP_POST,
        .handler = reset_post_handler,
    };
    httpd_register_uri_handler(server, &reset);

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_redirect_handler,
    };
    httpd_register_uri_handler(server, &root);

    ESP_LOGI(TAG, "Settings web server started on port %d", config.server_port);
    return ESP_OK;
}
