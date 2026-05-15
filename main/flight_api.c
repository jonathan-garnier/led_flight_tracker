#include "flight_api.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "flight_api";

#define MAX_RESPONSE_SIZE (32 * 1024)

static char s_api_username[65] = {0};
static char s_api_password[65] = {0};

void flight_api_set_credentials(const char *username, const char *password)
{
    if (username && username[0]) {
        strncpy(s_api_username, username, sizeof(s_api_username) - 1);
        strncpy(s_api_password, password ? password : "", sizeof(s_api_password) - 1);
        ESP_LOGI(TAG, "API credentials set for user: %s", s_api_username);
    } else {
        s_api_username[0] = '\0';
        s_api_password[0] = '\0';
        ESP_LOGI(TAG, "Using anonymous API access");
    }
}

typedef struct {
    char *buffer;
    int len;
    int capacity;
} response_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    response_buf_t *resp = (response_buf_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (resp->len + evt->data_len < resp->capacity) {
            memcpy(resp->buffer + resp->len, evt->data, evt->data_len);
            resp->len += evt->data_len;
            resp->buffer[resp->len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void parse_state_array(cJSON *state, flight_t *flight)
{
    memset(flight, 0, sizeof(*flight));

    // OpenSky state vector indices:
    // [0] icao24, [1] callsign, [2] origin_country, [3] time_position,
    // [4] last_contact, [5] longitude, [6] latitude, [7] baro_altitude,
    // [8] on_ground, [9] velocity, [10] true_track, ...

    cJSON *callsign_item = cJSON_GetArrayItem(state, 1);
    if (cJSON_IsString(callsign_item) && callsign_item->valuestring) {
        strncpy(flight->callsign, callsign_item->valuestring, MAX_CALLSIGN_LEN - 1);
        // Trim trailing whitespace (including all-spaces case)
        char *end = flight->callsign + strlen(flight->callsign) - 1;
        while (end >= flight->callsign && *end == ' ') *end-- = '\0';
    }

    cJSON *country_item = cJSON_GetArrayItem(state, 2);
    if (cJSON_IsString(country_item) && country_item->valuestring) {
        strncpy(flight->origin_country, country_item->valuestring, MAX_COUNTRY_LEN - 1);
    }

    // Prefer baro_altitude [7], fall back to geo_altitude [13]
    cJSON *alt_item = cJSON_GetArrayItem(state, 7);
    if (cJSON_IsNumber(alt_item)) {
        flight->altitude = (float)alt_item->valuedouble;
    } else {
        cJSON *geo_alt_item = cJSON_GetArrayItem(state, 13);
        if (cJSON_IsNumber(geo_alt_item)) {
            flight->altitude = (float)geo_alt_item->valuedouble;
        }
    }

    cJSON *vel_item = cJSON_GetArrayItem(state, 9);
    if (cJSON_IsNumber(vel_item)) {
        flight->velocity = (float)vel_item->valuedouble;
    }

    cJSON *heading_item = cJSON_GetArrayItem(state, 10);
    if (cJSON_IsNumber(heading_item)) {
        flight->heading = (float)heading_item->valuedouble;
    }

    cJSON *ground_item = cJSON_GetArrayItem(state, 8);
    flight->on_ground = cJSON_IsTrue(ground_item);
}

esp_err_t flight_api_fetch(float lamin, float lomin, float lamax, float lomax,
                           flight_data_t *data)
{
    data->count = 0;

    char url[256];
    snprintf(url, sizeof(url),
             "https://opensky-network.org/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
             lamin, lomin, lamax, lomax);

    response_buf_t resp = {
        .buffer = malloc(MAX_RESPONSE_SIZE),
        .len = 0,
        .capacity = MAX_RESPONSE_SIZE,
    };
    if (!resp.buffer) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .username = s_api_username[0] ? s_api_username : NULL,
        .password = s_api_username[0] ? s_api_password : NULL,
        .auth_type = s_api_username[0] ? HTTP_AUTH_TYPE_BASIC : HTTP_AUTH_TYPE_NONE,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(resp.buffer);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGE(TAG, "API returned status %d", status);
        free(resp.buffer);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "API response: %d bytes (buffer capacity: %d)", resp.len, resp.capacity);
    ESP_LOGI(TAG, "Request URL: %s", url);

    cJSON *root = cJSON_Parse(resp.buffer);
    free(resp.buffer);

    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_FAIL;
    }

    cJSON *states = cJSON_GetObjectItem(root, "states");
    if (!cJSON_IsArray(states)) {
        ESP_LOGI(TAG, "No flights in bounding box");
        cJSON_Delete(root);
        return ESP_OK;
    }

    int n = cJSON_GetArraySize(states);
    ESP_LOGI(TAG, "API returned %d total state vectors", n);
    if (n > MAX_FLIGHTS) n = MAX_FLIGHTS;

    for (int i = 0; i < n; i++) {
        cJSON *state = cJSON_GetArrayItem(states, i);
        if (cJSON_IsArray(state)) {
            parse_state_array(state, &data->flights[data->count]);
            flight_t *f = &data->flights[data->count];
            ESP_LOGI(TAG, "  %s: %s (%s) alt=%.0fm",
                     f->on_ground ? "GND" : "AIR",
                     f->callsign[0] ? f->callsign : "(none)",
                     f->origin_country, f->altitude);
            data->count++;
        }
    }

    ESP_LOGI(TAG, "Fetched %d flights", data->count);

    cJSON_Delete(root);
    return ESP_OK;
}
