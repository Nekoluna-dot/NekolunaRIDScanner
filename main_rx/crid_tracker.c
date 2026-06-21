/**
 * crid_tracker.c — 无人机追踪表（线程安全）
 */

#include <string.h>
#include "esp_log.h"
#include "crid_tracker.h"
#include "crid_json.h"

/* ---- 模块内部状态 ---- */

static SemaphoreHandle_t g_tracker_mutex = NULL;
static uav_track_t       g_uavs[MAX_TRACKED_UAVS];
static uint32_t          g_start_time_ms = 0;

/* ---- MAC 比较 ---- */

static inline bool mac_equal(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, 6) == 0;
}

/* ---- 公开接口 ---- */

void crid_tracker_init(void) {
    memset(g_uavs, 0, sizeof(g_uavs));
    g_start_time_ms = esp_log_timestamp();

    g_tracker_mutex = xSemaphoreCreateMutex();
    if (g_tracker_mutex == NULL) {
        json_error("RID_TRACK", "Failed to create tracker mutex!");
    }
}

SemaphoreHandle_t crid_tracker_get_mutex(void) {
    return g_tracker_mutex;
}

uav_track_t *crid_tracker_find_or_create(const uint8_t *mac) {
    uav_track_t *free_slot = NULL;

    for (int i = 0; i < MAX_TRACKED_UAVS; i++) {
        if (g_uavs[i].active) {
            if (mac_equal(g_uavs[i].mac, mac)) {
                return &g_uavs[i];
            }
        } else if (free_slot == NULL) {
            free_slot = &g_uavs[i];
        }
    }

    if (free_slot) {
        memset(free_slot, 0, sizeof(uav_track_t));
        memcpy(free_slot->mac, mac, 6);
        free_slot->active = true;
        free_slot->first_seen_ms = esp_log_timestamp();
        odid_initUasData(&free_slot->uas_data);
        return free_slot;
    }

    return NULL;  // 追踪表已满
}

int crid_tracker_get_active_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_TRACKED_UAVS; i++) {
        if (g_uavs[i].active) count++;
    }
    return count;
}

uav_track_t *crid_tracker_get_table(void) {
    return g_uavs;
}

void crid_tracker_cleanup(uint32_t timeout_ms) {
    uint32_t now = esp_log_timestamp();
    for (int i = 0; i < MAX_TRACKED_UAVS; i++) {
        if (!g_uavs[i].active) continue;
        uint32_t age_ms = now - g_uavs[i].last_seen_ms;
        if (age_ms > timeout_ms) {
            json_uav_timeout(g_uavs[i].mac);
            g_uavs[i].active = false;
        }
    }
}
