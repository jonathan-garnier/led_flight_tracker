#include "time_sync.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>

static const char* TAG = "time_sync";
static bool sntp_ready = false;

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized");
    sntp_ready = true;
}

void time_sync_start()
{
    ESP_LOGI(TAG, "Starting SNTP");

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}

bool time_sync_ready()
{
    return sntp_ready;
}
