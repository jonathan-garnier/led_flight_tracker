#include "config_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "config_storage";
static const char *NVS_NAMESPACE = "flight_cfg";

esp_err_t config_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t config_storage_save(const device_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_str(handle, "ssid", config->ssid);
    nvs_set_str(handle, "password", config->password);
    nvs_set_str(handle, "api_cid", config->api_client_id);
    nvs_set_str(handle, "api_csec", config->api_client_secret);

    // Store floats as raw blobs
    nvs_set_blob(handle, "lamin", &config->lamin, sizeof(float));
    nvs_set_blob(handle, "lomin", &config->lomin, sizeof(float));
    nvs_set_blob(handle, "lamax", &config->lamax, sizeof(float));
    nvs_set_blob(handle, "lomax", &config->lomax, sizeof(float));

    ret = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Config saved: SSID=%s, bbox=[%.4f,%.4f,%.4f,%.4f]",
             config->ssid, config->lamin, config->lomin, config->lamax, config->lomax);
    return ret;
}

esp_err_t config_storage_load(device_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) return ret;

    size_t len;

    len = MAX_SSID_LEN;
    ret = nvs_get_str(handle, "ssid", config->ssid, &len);
    if (ret != ESP_OK) goto done;

    len = MAX_PASS_LEN;
    ret = nvs_get_str(handle, "password", config->password, &len);
    if (ret != ESP_OK) goto done;

    len = MAX_API_CLIENT_ID_LEN;
    nvs_get_str(handle, "api_cid", config->api_client_id, &len);
    len = MAX_API_CLIENT_SECRET_LEN;
    nvs_get_str(handle, "api_csec", config->api_client_secret, &len);

    len = sizeof(float);
    nvs_get_blob(handle, "lamin", &config->lamin, &len);
    nvs_get_blob(handle, "lomin", &config->lomin, &len);
    nvs_get_blob(handle, "lamax", &config->lamax, &len);
    nvs_get_blob(handle, "lomax", &config->lomax, &len);

    ESP_LOGI(TAG, "Config loaded: SSID=%s, API client=%s, bbox=[%.4f,%.4f,%.4f,%.4f]",
             config->ssid, config->api_client_id, config->lamin, config->lomin, config->lamax, config->lomax);

done:
    nvs_close(handle);
    return ret;
}

bool config_storage_exists(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;

    size_t len = 0;
    esp_err_t ret = nvs_get_str(handle, "ssid", NULL, &len);
    nvs_close(handle);
    return (ret == ESP_OK && len > 1);
}
