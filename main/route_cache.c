#include "route_cache.h"
#include "flight_api.h"   // MAX_CALLSIGN_LEN, MAX_AIRPORT_LEN
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "route_cache";

#define ROUTE_CACHE_MAX 256
#define NVS_NAMESPACE   "route_cache"
#define NVS_KEY_ENTRIES "entries"
#define NVS_KEY_COUNT   "count"

typedef struct {
    char callsign[MAX_CALLSIGN_LEN];  // 9
    char origin[MAX_AIRPORT_LEN];     // 5
    char dest[MAX_AIRPORT_LEN];       // 5
    uint8_t status;                   // ROUTE_CACHE_FOUND / ROUTE_CACHE_NONE
    uint32_t last_used;               // LRU tick
} route_entry_t;

static route_entry_t s_entries[ROUTE_CACHE_MAX];
static int s_count = 0;
static uint32_t s_tick = 0;
static bool s_dirty = false;
static SemaphoreHandle_t s_mutex = NULL;

static void lock(void)   { if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void unlock(void) { if (s_mutex) xSemaphoreGive(s_mutex); }

// Find index of a callsign, or -1. Caller holds the lock.
static int find_index(const char *callsign)
{
    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_entries[i].callsign, callsign, MAX_CALLSIGN_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

// Index of the least-recently-used entry. Caller holds the lock.
static int lru_index(void)
{
    int lru = 0;
    for (int i = 1; i < s_count; i++) {
        if (s_entries[i].last_used < s_entries[lru].last_used) lru = i;
    }
    return lru;
}

void route_cache_init(void)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();

    lock();
    s_count = 0;
    s_tick = 0;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint16_t count = 0;
        if (nvs_get_u16(h, NVS_KEY_COUNT, &count) == ESP_OK && count > 0) {
            if (count > ROUTE_CACHE_MAX) count = ROUTE_CACHE_MAX;
            size_t want = (size_t)count * sizeof(route_entry_t);
            size_t got = want;
            if (nvs_get_blob(h, NVS_KEY_ENTRIES, s_entries, &got) == ESP_OK && got == want) {
                s_count = count;
                for (int i = 0; i < s_count; i++) {
                    if (s_entries[i].last_used >= s_tick) s_tick = s_entries[i].last_used + 1;
                }
            }
        }
        nvs_close(h);
    }
    s_dirty = false;
    ESP_LOGI(TAG, "Loaded %d cached routes", s_count);
    unlock();
}

route_cache_status_t route_cache_lookup(const char *callsign, char origin[5], char dest[5])
{
    origin[0] = '\0';
    dest[0] = '\0';
    if (!callsign || !callsign[0]) return ROUTE_CACHE_MISS;

    lock();
    int i = find_index(callsign);
    route_cache_status_t result = ROUTE_CACHE_MISS;
    if (i >= 0) {
        s_entries[i].last_used = s_tick++;
        if (s_entries[i].status == ROUTE_CACHE_FOUND) {
            strncpy(origin, s_entries[i].origin, MAX_AIRPORT_LEN);
            strncpy(dest, s_entries[i].dest, MAX_AIRPORT_LEN);
            result = ROUTE_CACHE_FOUND;
        } else {
            result = ROUTE_CACHE_NONE;
        }
    }
    unlock();
    return result;
}

void route_cache_put(const char *callsign, const char *origin, const char *dest, bool found)
{
    if (!callsign || strlen(callsign) < 2) return;

    lock();
    int i = find_index(callsign);
    if (i < 0) {
        if (s_count < ROUTE_CACHE_MAX) {
            i = s_count++;
        } else {
            i = lru_index();  // evict LRU
        }
    }
    memset(&s_entries[i], 0, sizeof(s_entries[i]));
    strncpy(s_entries[i].callsign, callsign, MAX_CALLSIGN_LEN - 1);
    if (found) {
        s_entries[i].status = ROUTE_CACHE_FOUND;
        strncpy(s_entries[i].origin, origin ? origin : "", MAX_AIRPORT_LEN - 1);
        strncpy(s_entries[i].dest, dest ? dest : "", MAX_AIRPORT_LEN - 1);
    } else {
        s_entries[i].status = ROUTE_CACHE_NONE;
    }
    s_entries[i].last_used = s_tick++;
    s_dirty = true;
    unlock();
}

void route_cache_flush(void)
{
    // Flushes are infrequent (batched), so we hold the lock across the NVS
    // write rather than copying the whole 6 KB blob onto the task stack.
    lock();
    if (!s_dirty) {
        unlock();
        return;
    }
    size_t blob_size = (size_t)s_count * sizeof(route_entry_t);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY_ENTRIES, s_entries, blob_size);
        if (err == ESP_OK) err = nvs_set_u16(h, NVS_KEY_COUNT, (uint16_t)s_count);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }

    if (err == ESP_OK) {
        s_dirty = false;
        ESP_LOGI(TAG, "Flushed %d routes to NVS (%u bytes)", s_count, (unsigned)blob_size);
    } else {
        ESP_LOGW(TAG, "flush failed: %s", esp_err_to_name(err));  // stays dirty, retry later
    }
    unlock();
}
