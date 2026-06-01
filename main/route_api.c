#include "route_api.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "route_api";

#define ROUTE_RESPONSE_SIZE 4096
#define ROUTE_HOST "https://vrs-standing-data.adsb.lol"

typedef struct {
    char *buffer;
    int len;
    int capacity;
} route_resp_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    route_resp_t *resp = (route_resp_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && resp) {
        if (resp->len + evt->data_len < resp->capacity) {
            memcpy(resp->buffer + resp->len, evt->data, evt->data_len);
            resp->len += evt->data_len;
            resp->buffer[resp->len] = '\0';
        }
    }
    return ESP_OK;
}

// Copy a candidate airport code into a >=5-byte buffer, uppercased.
// Returns true if it looks like a usable code (2-4 alpha/num chars).
static bool copy_code(char out[5], const char *src)
{
    out[0] = '\0';
    if (!src) return false;
    while (*src == ' ') src++;             // skip leading spaces
    int n = 0;
    for (; src[n] && src[n] != ' ' && n < 4; n++) {
        out[n] = (char)toupper((unsigned char)src[n]);
    }
    out[n] = '\0';
    return (n >= 2);
}

// Parse "AAA-BBB" (or multi-leg "AAA-BBB-CCC") into origin (first) and dest (last).
static bool parse_dash_codes(const char *s, char origin[5], char dest[5])
{
    if (!s) return false;
    const char *first_dash = strchr(s, '-');
    if (!first_dash) return false;        // need at least two airports
    const char *last_dash = strrchr(s, '-');

    char tmp[8];
    // origin = substring before the first dash
    int olen = (int)(first_dash - s);
    if (olen <= 0 || olen >= (int)sizeof(tmp)) return false;
    memcpy(tmp, s, olen);
    tmp[olen] = '\0';
    if (!copy_code(origin, tmp)) return false;

    // dest = substring after the last dash
    return copy_code(dest, last_dash + 1);
}

// Pull the iata (preferred) or icao code from an _airports[] array element.
static bool code_from_airport_obj(cJSON *obj, char out[5])
{
    if (!cJSON_IsObject(obj)) return false;
    cJSON *iata = cJSON_GetObjectItem(obj, "iata");
    if (cJSON_IsString(iata) && copy_code(out, iata->valuestring)) return true;
    cJSON *icao = cJSON_GetObjectItem(obj, "icao");
    if (cJSON_IsString(icao) && copy_code(out, icao->valuestring)) return true;
    return false;
}

static route_api_result_t parse_route_json(const char *json, char origin[5], char dest[5])
{
    origin[0] = '\0';
    dest[0] = '\0';

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "route JSON parse failed");
        return ROUTE_API_ERROR;
    }

    route_api_result_t result = ROUTE_API_NOT_FOUND;

    // Preferred: "_airport_codes_iata": "BNE-SYD"
    cJSON *codes = cJSON_GetObjectItem(root, "_airport_codes_iata");
    if (cJSON_IsString(codes) && parse_dash_codes(codes->valuestring, origin, dest)) {
        result = ROUTE_API_FOUND;
    } else {
        // Fallback: "_airports": [ {origin}, ..., {dest} ]
        cJSON *airports = cJSON_GetObjectItem(root, "_airports");
        if (cJSON_IsArray(airports)) {
            int n = cJSON_GetArraySize(airports);
            if (n >= 2 &&
                code_from_airport_obj(cJSON_GetArrayItem(airports, 0), origin) &&
                code_from_airport_obj(cJSON_GetArrayItem(airports, n - 1), dest)) {
                result = ROUTE_API_FOUND;
            }
        }
    }

    if (result != ROUTE_API_FOUND) {
        origin[0] = '\0';
        dest[0] = '\0';
    }
    cJSON_Delete(root);
    return result;
}

route_api_result_t route_api_fetch(const char *callsign, char origin[5], char dest[5])
{
    origin[0] = '\0';
    dest[0] = '\0';

    if (!callsign || strlen(callsign) < 2) {
        return ROUTE_API_NOT_FOUND;
    }

    // URL: <host>/routes/<first 2 chars>/<CALLSIGN>.json
    char url[128];
    snprintf(url, sizeof(url), "%s/routes/%c%c/%s.json",
             ROUTE_HOST, callsign[0], callsign[1], callsign);

    route_resp_t resp = {
        .buffer = malloc(ROUTE_RESPONSE_SIZE),
        .len = 0,
        .capacity = ROUTE_RESPONSE_SIZE,
    };
    if (!resp.buffer) return ROUTE_API_ERROR;
    resp.buffer[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: request failed: %s", callsign, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(resp.buffer);
        return ROUTE_API_ERROR;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    route_api_result_t result;
    if (status == 404) {
        ESP_LOGI(TAG, "%s: no route in database (404)", callsign);
        result = ROUTE_API_NOT_FOUND;
    } else if (status == 200) {
        result = parse_route_json(resp.buffer, origin, dest);
        if (result == ROUTE_API_FOUND) {
            ESP_LOGI(TAG, "%s: %s -> %s", callsign, origin, dest);
        } else if (result == ROUTE_API_NOT_FOUND) {
            ESP_LOGI(TAG, "%s: 200 but no usable codes", callsign);
        }
    } else {
        ESP_LOGW(TAG, "%s: unexpected status %d", callsign, status);
        result = ROUTE_API_ERROR;
    }

    free(resp.buffer);
    return result;
}
