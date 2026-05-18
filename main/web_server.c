#include "web_server.h"
#include "config_storage.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
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
