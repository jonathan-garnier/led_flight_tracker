#include "wifi_manager.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "WiFiManager";

// Singleton instance
WiFiManager& WiFiManager::instance() {
    static WiFiManager inst;
    return inst;
}

// ---------------------------------------------------
// Internal setter methods
// ---------------------------------------------------
void WiFiManager::setState(WiFiState s)
{
    state = s;
}

void WiFiManager::setIPAddress(const char* ip)
{
    strncpy(ipStr, ip, sizeof(ipStr));
    ipStr[sizeof(ipStr)-1] = 0;
}

// ---------------------------------------------------
// Load saved Wi-Fi credentials from NVS
// ---------------------------------------------------
void WiFiManager::loadCredentials()
{
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK) {
        ssid[0] = 0;
        pwd[0] = 0;
        return;
    }

    size_t s1 = sizeof(ssid);
    size_t s2 = sizeof(pwd);

    if (nvs_get_str(nvs, "ssid", ssid, &s1) != ESP_OK) ssid[0] = 0;
    if (nvs_get_str(nvs, "pwd",  pwd,  &s2) != ESP_OK) pwd[0]  = 0;

    nvs_close(nvs);
}

bool WiFiManager::hasSavedCredentials()
{
    return strlen(ssid) > 0;
}

// ---------------------------------------------------
// Save credentials to NVS
// ---------------------------------------------------
void WiFiManager::saveCredentials(const char* s, const char* p)
{
    nvs_handle_t nvs;
    nvs_open("wifi", NVS_READWRITE, &nvs);

    nvs_set_str(nvs, "ssid", s);
    nvs_set_str(nvs, "pwd",  p);
    nvs_commit(nvs);
    nvs_close(nvs);

    strncpy(ssid, s, sizeof(ssid));
    strncpy(pwd, p, sizeof(pwd));
}

// ---------------------------------------------------
// Clear credentials (factory reset)
// ---------------------------------------------------
void WiFiManager::clearCredentials()
{
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, "ssid");
        nvs_erase_key(nvs, "pwd");
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    ssid[0] = 0;
    pwd[0] = 0;

    ESP_LOGI(TAG, "WiFi credentials cleared");
}

// ---------------------------------------------------
// Wi-Fi Event Handler
// ---------------------------------------------------
static void wifi_event_handler(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data)
{
    WiFiManager& wm = WiFiManager::instance();

    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            wm.setState(WiFiState::CONNECTING);
            esp_wifi_connect();
        }

        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wm.setState(WiFiState::FAILED);
            ESP_LOGW(TAG, "STA disconnected");
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto* event = (ip_event_got_ip_t*)event_data;

        char ipbuf[16];
        snprintf(ipbuf, sizeof(ipbuf), IPSTR, IP2STR(&event->ip_info.ip));

        wm.setIPAddress(ipbuf);
        wm.setState(WiFiState::CONNECTED);

        ESP_LOGI(TAG, "Connected! IP: %s", wm.getIPAddress());
    }
}

// ---------------------------------------------------
// Start WiFi in STA mode (connect to router)
// ---------------------------------------------------
void WiFiManager::startSTA(const char* ssid_in, const char* pwd_in)
{
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr);

    wifi_config_t staConfig = {};
    strcpy((char*)staConfig.sta.ssid, ssid_in);
    strcpy((char*)staConfig.sta.password, pwd_in);

    ESP_LOGI(TAG, "Starting STA, ssid=%s", ssid_in);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &staConfig);
    esp_wifi_start();

    setState(WiFiState::CONNECTING);
}

// ---------------------------------------------------
// Start AP mode (for Wi-Fi setup)
// ---------------------------------------------------
void WiFiManager::startAP()
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t apConfig = {};
    strcpy((char*)apConfig.ap.ssid, "LED-Flight-Tracker");
    strcpy((char*)apConfig.ap.password, "12345678");

    apConfig.ap.channel = 1;
    apConfig.ap.ssid_len = strlen("LED-Flight-Tracker");
    apConfig.ap.max_connection = 4;
    apConfig.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    ESP_LOGI(TAG, "Starting AP Mode...");

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &apConfig);
    esp_wifi_start();

    setState(WiFiState::AP_MODE);
    setIPAddress("192.168.4.1");
}

// ---------------------------------------------------
// Begin: Initialize WiFi according to saved credentials
// ---------------------------------------------------
void WiFiManager::begin()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    loadCredentials();

    if (hasSavedCredentials()) {
        ESP_LOGI(TAG, "Credentials found. Starting STA...");
        startSTA(ssid, pwd);
    } else {
        ESP_LOGI(TAG, "No saved credentials. Starting AP...");
        startAP();
    }
}

// ---------------------------------------------------
// Getters
// ---------------------------------------------------
WiFiState WiFiManager::getState() { return state; }
const char* WiFiManager::getSSID() { return ssid; }
const char* WiFiManager::getIPAddress() { return ipStr; }
