#include "flight_api.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "flight_api";

#define MAX_RESPONSE_SIZE (32 * 1024)
#define TOKEN_URL "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"
#define TOKEN_REFRESH_MARGIN_S 120  // refresh 2 min before expiry

static char s_client_id[65] = {0};
static char s_client_secret[65] = {0};
static char *s_access_token = NULL;
static int64_t s_token_expiry = 0;  // monotonic time in microseconds
static int s_api_call_count = 0;

void flight_api_set_oauth_credentials(const char *client_id, const char *client_secret)
{
    if (client_id && client_id[0]) {
        strncpy(s_client_id, client_id, sizeof(s_client_id) - 1);
        strncpy(s_client_secret, client_secret ? client_secret : "", sizeof(s_client_secret) - 1);
        ESP_LOGI(TAG, "OAuth2 credentials set for client: %s", s_client_id);
    } else {
        s_client_id[0] = '\0';
        s_client_secret[0] = '\0';
        ESP_LOGI(TAG, "No OAuth2 credentials configured");
    }
}

typedef struct {
    char *buffer;
    int len;
    int capacity;
    int rate_limit_remaining;  // -1 if header not seen
} response_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    response_buf_t *resp = (response_buf_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
        if (strcasecmp(evt->header_key, "x-rate-limit-remaining") == 0) {
            resp->rate_limit_remaining = atoi(evt->header_value);
        }
        break;
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

// Obtain a new OAuth2 access token using client credentials grant
static esp_err_t obtain_token(void)
{
    if (!s_client_id[0]) {
        ESP_LOGE(TAG, "No OAuth2 credentials configured");
        return ESP_ERR_INVALID_STATE;
    }

    // Build POST body
    char post_data[256];
    snprintf(post_data, sizeof(post_data),
             "grant_type=client_credentials&client_id=%s&client_secret=%s",
             s_client_id, s_client_secret);

    response_buf_t resp = {
        .buffer = malloc(4096),
        .len = 0,
        .capacity = 4096,
        .rate_limit_remaining = -1,
    };
    if (!resp.buffer) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = TOKEN_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Token request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(resp.buffer);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGE(TAG, "Token endpoint returned %d - credentials rejected", status);
        free(resp.buffer);
        return ESP_ERR_INVALID_ARG;
    }

    // Parse token response
    cJSON *root = cJSON_Parse(resp.buffer);
    free(resp.buffer);

    if (!root) {
        ESP_LOGE(TAG, "Token JSON parse failed");
        return ESP_FAIL;
    }

    cJSON *token_item = cJSON_GetObjectItem(root, "access_token");
    cJSON *expires_item = cJSON_GetObjectItem(root, "expires_in");

    if (!cJSON_IsString(token_item) || !token_item->valuestring[0]) {
        ESP_LOGE(TAG, "No access_token in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Store the new token
    free(s_access_token);
    s_access_token = strdup(token_item->valuestring);
    if (!s_access_token) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    int expires_in = cJSON_IsNumber(expires_item) ? expires_item->valueint : 1800;
    s_token_expiry = esp_timer_get_time() +
                     (int64_t)(expires_in - TOKEN_REFRESH_MARGIN_S) * 1000000LL;

    ESP_LOGI(TAG, "OAuth2 token obtained (expires in %ds, refresh in %ds)",
             expires_in, expires_in - TOKEN_REFRESH_MARGIN_S);

    cJSON_Delete(root);
    return ESP_OK;
}

// Ensure we have a valid token, refreshing if needed
static esp_err_t ensure_token(void)
{
    if (!s_client_id[0]) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_access_token && esp_timer_get_time() < s_token_expiry) {
        return ESP_OK;  // token still valid
    }

    ESP_LOGI(TAG, "Token expired or missing, obtaining new token...");
    return obtain_token();
}

esp_err_t flight_api_validate_credentials(void)
{
    // Step 1: try to get a token
    esp_err_t err = obtain_token();
    if (err != ESP_OK) {
        return err;
    }

    // Step 2: make a small test request and check x-rate-limit-remaining
    // Use a tiny bounding box to minimize data
    const char *test_url = "https://opensky-network.org/api/states/all?lamin=0&lomin=0&lamax=0.01&lomax=0.01";

    response_buf_t resp = {
        .buffer = malloc(4096),
        .len = 0,
        .capacity = 4096,
        .rate_limit_remaining = -1,
    };
    if (!resp.buffer) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = test_url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Set Bearer token
    char auth_header[2048];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_access_token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Validation request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(resp.buffer);
        return ESP_FAIL;
    }

    int status = esp_http_client_get_status_code(client);

    if (status == 401) {
        ESP_LOGE(TAG, "API rejected Bearer token (401)");
        esp_http_client_cleanup(client);
        free(resp.buffer);
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_cleanup(client);

    if (status == 200) {
        if (resp.rate_limit_remaining > 400) {
            ESP_LOGI(TAG, "Credentials validated: authenticated tier (remaining: %d credits)",
                     resp.rate_limit_remaining);
            free(resp.buffer);
            return ESP_OK;
        } else if (resp.rate_limit_remaining >= 0) {
            ESP_LOGE(TAG, "Credentials NOT working: running as anonymous (remaining: %d/400)",
                     resp.rate_limit_remaining);
            free(resp.buffer);
            return ESP_ERR_INVALID_ARG;
        } else {
            // No rate limit header seen - assume OK if token was accepted
            ESP_LOGW(TAG, "No rate-limit header in response, assuming authenticated");
            free(resp.buffer);
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "Validation failed with status %d", status);
    free(resp.buffer);
    return ESP_FAIL;
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

    // Ensure we have a valid OAuth2 token
    esp_err_t err = ensure_token();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot fetch flights - no valid token");
        return err;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "https://opensky-network.org/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
             lamin, lomin, lamax, lomax);

    response_buf_t resp = {
        .buffer = malloc(MAX_RESPONSE_SIZE),
        .len = 0,
        .capacity = MAX_RESPONSE_SIZE,
        .rate_limit_remaining = -1,
    };
    if (!resp.buffer) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Set Bearer token
    char auth_header[2048];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_access_token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(resp.buffer);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    s_api_call_count++;

    if (status == 401) {
        ESP_LOGW(TAG, "API returned 401 - token may have expired, forcing refresh");
        // Invalidate token and retry once
        free(s_access_token);
        s_access_token = NULL;
        s_token_expiry = 0;
        free(resp.buffer);
        return ESP_FAIL;
    }

    if (status == 429) {
        ESP_LOGE(TAG, "API returned 429 Too Many Requests - rate limited! (call #%d today)", s_api_call_count);
        free(resp.buffer);
        return ESP_FAIL;
    }

    if (status != 200) {
        ESP_LOGE(TAG, "API returned status %d", status);
        free(resp.buffer);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "API response: %d bytes, call #%d today, credits remaining: %d",
             resp.len, s_api_call_count, resp.rate_limit_remaining);

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
