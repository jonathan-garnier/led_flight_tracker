#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Arduino.h"
#include "esp_wifi.h"

#include "led_matrix.h"
#include "screen_manager.h"
#include "button.h"
#include "info_screen.h"
#include "spectrum_screen.h"
#include "fireworks_screen.h"
#include "clock_screen.h"
#include "flight_screen.h"
#include "wifi_manager.h"
#include "app_config.h"
#include "web_server.h"
#include "time_sync.h"
#include "flight_api.h"

#define BUTTON_PIN GPIO_NUM_38   // your button pin

static const int FRAME_RATE = 60;
static const TickType_t FRAME_DELAY = pdMS_TO_TICKS(1000 / FRAME_RATE);

extern "C" void app_main(void)
{
    initArduino();

    // Initialize app configuration
    AppConfig::instance().begin();

    // Initialize time sync with configured timezone
    TimeConfig tc = AppConfig::instance().getTimeConfig();
    time_sync_init(tc.timezone);

    // Initialize WiFi
    WiFiManager::instance().begin();

    // Initialize FlightAPI
    FlightAPI::instance().begin();

    // Start web server if in AP mode
    if (WiFiManager::instance().getState() == WiFiState::AP_MODE) {
        printf("Starting in AP mode, launching web server\n");
        WebServer::instance().begin();
    }

    // Start SNTP if WiFi is already connected
    if (WiFiManager::instance().getState() == WiFiState::CONNECTED) {
        printf("WiFi connected, starting SNTP\n");
        time_sync_start();
    }

    LEDMatrix matrix(64, 32, 1);
    matrix.begin();

    // Screen Manager - order: Flight Tracker -> Clock -> Spectrum -> Fireworks -> Info
    ScreenManager manager(matrix);
    manager.addScreen(new FlightScreen());
    manager.addScreen(new ClockScreen());
    manager.addScreen(new SpectrumScreen());
    manager.addScreen(new FireworksScreen());
    manager.addScreen(new InfoScreen());

    // Button
    Button button(BUTTON_PIN, true); // active low with pull-up
    button.begin();

    uint32_t lastTick = xTaskGetTickCount();
    WiFiState lastWiFiState = WiFiManager::instance().getState();

    while (true)
    {
        uint32_t now = xTaskGetTickCount();
        float dt = (now - lastTick) / 1000.0f;
        lastTick = now;

        // Check if WiFi reconnection is needed after configuration
        if (WebServer::instance().shouldReconnect()) {
            WebServer::instance().clearReconnectFlag();
            printf("Reconnecting to WiFi with new credentials...\n");

            // Stop current WiFi
            esp_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds for cleanup

            // Reinitialize WiFi manager (will auto-connect with new credentials)
            WiFiManager::instance().begin();
        }

        // Monitor WiFi state and manage web server + SNTP
        WiFiState currentWiFiState = WiFiManager::instance().getState();
        if (currentWiFiState != lastWiFiState) {
            lastWiFiState = currentWiFiState;

            if (currentWiFiState == WiFiState::AP_MODE) {
                // Entered AP mode - start web server
                printf("Entered AP mode, starting web server\n");
                WebServer::instance().begin();
            }
            else if (currentWiFiState == WiFiState::CONNECTED) {
                // Just connected to WiFi - start SNTP
                printf("WiFi connected, starting SNTP\n");
                time_sync_start();

                // Start web server in STA mode for settings updates
                if (!WebServer::instance().isRunning()) {
                    printf("Starting web server in STA mode for settings updates\n");
                    WebServer::instance().begin(ServerMode::STA_MODE);
                } else {
                    // If already running (from AP mode), switch to STA mode
                    printf("Switching web server to STA mode\n");
                    WebServer::instance().setMode(ServerMode::STA_MODE);
                }

                // Trigger an immediate flight fetch (if we have location configured)
                LocationConfig loc = AppConfig::instance().getLocation();
                if (loc.valid) {
                    printf("Fetching initial flight data...\n");
                    FlightConfig fc = AppConfig::instance().getFlightConfig();
                    // Validate bbox before fetching
                    if (fc.lat_min < fc.lat_max && fc.lon_min < fc.lon_max) {
                        FlightAPI::instance().fetchFlights(fc.lat_min, fc.lat_max, fc.lon_min, fc.lon_max);
                    } else {
                        printf("Warning: Invalid bounding box\n");
                    }
                }
            }
            else {
                // Left AP mode - stop web server
                if (WebServer::instance().isRunning()) {
                    printf("Left AP mode, stopping web server\n");
                    WebServer::instance().stop();
                }
            }
        }

        // Update button
        button.update();

        // Check for long press (5 seconds) - factory reset WiFi credentials
        if (button.wasLongPressed(5000))
        {
            printf("LONG PRESS DETECTED! Clearing WiFi credentials...\n");
            WiFiManager::instance().clearCredentials();

            // Stop WiFi and return to AP mode
            esp_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(1000));
            WiFiManager::instance().startAP();

            printf("WiFi credentials cleared. Device now in AP mode.\n");
        }

        // Check for short press - cycle screens
        if (button.wasPressed())
        {
            printf("BUTTON PRESSED!\n");
            manager.nextScreen();
        }

        // Periodic flight data fetch (every 5 minutes when WiFi connected)
        if (currentWiFiState == WiFiState::CONNECTED) {
            if (FlightAPI::instance().canFetch()) {
                LocationConfig loc = AppConfig::instance().getLocation();
                if (loc.valid) {
                    FlightConfig fc = AppConfig::instance().getFlightConfig();
                    // Validate bbox before fetching
                    if (fc.lat_min < fc.lat_max && fc.lon_min < fc.lon_max) {
                        FlightAPI::instance().fetchFlights(fc.lat_min, fc.lat_max, fc.lon_min, fc.lon_max);
                        printf("Flight fetch complete: %zu flights found\n", FlightAPI::instance().getFlightCount());
                    }
                }
            }
        }

        // Update + render active screen
        manager.update(dt);
        manager.render();
        matrix.show();

        vTaskDelay(FRAME_DELAY);
    }
}
