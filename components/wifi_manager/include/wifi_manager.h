#pragma once

#include <string>

enum class WiFiState {
    DISCONNECTED,
    AP_MODE,
    CONNECTING,
    CONNECTED,
    FAILED
};

class WiFiManager {
public:
    static WiFiManager& instance();

    void begin(); // start AP or STA depending on NVS
    void startAP();
    void startSTA(const char* ssid, const char* pwd);

    // Getters
    WiFiState getState();
    const char* getSSID();
    const char* getIPAddress();

    // NVS credentials
    bool hasSavedCredentials();
    void saveCredentials(const char* ssid, const char* pwd);
    void loadCredentials();

    // Setters (MUST exist for event handler)
    void setState(WiFiState s);
    void setIPAddress(const char* ip);

private:
    WiFiManager() = default; // Singleton

    WiFiState state = WiFiState::DISCONNECTED;

    char ssid[32] = {0};
    char pwd[64]  = {0};
    char ipStr[16] = "0.0.0.0";
};
