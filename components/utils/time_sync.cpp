#include "time_sync.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <string.h>

static const char* TAG = "time_sync";
static bool sntp_ready = false;

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized");
    sntp_ready = true;
}

void time_sync_init(const char* timezone)
{
    ESP_LOGI(TAG, "Initializing time sync with timezone: %s", timezone);

    // Set timezone using POSIX format
    setenv("TZ", timezone, 1);
    tzset();
}

void time_sync_start()
{
    ESP_LOGI(TAG, "Starting SNTP");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");  // Backup server
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH); // Gradual adjustment
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

bool time_sync_ready()
{
    return sntp_ready;
}

void time_sync_get_time(struct tm* timeinfo)
{
    time_t now = time(NULL);
    localtime_r(&now, timeinfo);
}

void time_sync_set_timezone(const char* tz)
{
    ESP_LOGI(TAG, "Updating timezone to: %s", tz);
    setenv("TZ", tz, 1);
    tzset();
}
