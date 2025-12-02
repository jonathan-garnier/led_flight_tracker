#pragma once

#include <time.h>

void time_sync_init(const char* timezone);  // Initialize with timezone
void time_sync_start();                     // Start SNTP (after WiFi connected)
bool time_sync_ready();                     // Check if time is synced
void time_sync_get_time(struct tm* timeinfo); // Get local time
void time_sync_set_timezone(const char* tz);  // Update timezone
