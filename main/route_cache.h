#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ROUTE_CACHE_MISS = 0,  // callsign not in cache - needs a network lookup
    ROUTE_CACHE_FOUND,     // cached route, origin/dest populated
    ROUTE_CACHE_NONE,      // cached "no route in database" (negative entry)
} route_cache_status_t;

// Load the persisted cache from NVS. Call once at startup (after nvs_flash_init).
void route_cache_init(void);

// Look up a callsign. On ROUTE_CACHE_FOUND, origin/dest (>=5 bytes) are filled.
// Marks the entry as recently used.
route_cache_status_t route_cache_lookup(const char *callsign, char origin[5], char dest[5]);

// Insert or update a cache entry. `found` selects FOUND vs NONE (negative cache).
// Marks the cache dirty; call route_cache_flush() to persist.
void route_cache_put(const char *callsign, const char *origin, const char *dest, bool found);

// Persist to NVS if anything changed since the last flush. Cheap no-op if clean.
void route_cache_flush(void);

#ifdef __cplusplus
}
#endif
