/**
 * crid_json.c — JSON 格式化输出模块实现
 *
 * 双输出流设计：
 *   - 数据流：UAV 解析数据、状态统计 → stdout + 可选 UART 回调
 *   - 调试流：启动信息、调试、告警、错误、解码诊断 → stdout（USB CDC）
 *
 * 格式约定：
 *   - 顶层字段: {"evt":"类型", "ts":毫秒时间戳, ...}
 *   - 所有数值使用原始格式（不转字符串）
 *   - 字符串字段做 JSON 转义
 *   - 无尾随逗号
 *
 * 时间戳使用 esp_log_timestamp() 获取毫秒级运行时间。
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "opendroneid.h"
#include "crid_json.h"
#include "crid_display.h"

/* ================================================================
 * 双输出流状态
 * ================================================================ */

static json_write_cb_t g_data_write_cb = NULL;  // 数据流回调（UAV 数据、状态统计）
static void *g_data_write_ctx = NULL;            // 回调上下文

// 数据流：输出到 stdout + 可选回调
static inline void data_write(const char *str, size_t len) {
    fwrite(str, 1, len, stdout);
    if (g_data_write_cb) {
        g_data_write_cb(str, len, g_data_write_ctx);
    }
}

// 调试流：仅输出到 stdout
static inline void debug_write(const char *str, size_t len) {
    fwrite(str, 1, len, stdout);
}

void json_set_data_write_cb(json_write_cb_t cb, void *ctx) {
    g_data_write_cb = cb;
    g_data_write_ctx = ctx;
}
json_write_cb_t json_get_data_write_cb(void) { return g_data_write_cb; }
void *json_get_data_write_ctx(void)           { return g_data_write_ctx; }

// 数据输出宏：snprintf 到栈缓冲区，通过 data_write 输出
#define DATA_PRINTF(fmt, ...) do { \
    char _b[512]; \
    int _n = snprintf(_b, sizeof(_b), fmt, ##__VA_ARGS__); \
    if (_n > 0) data_write(_b, (size_t)_n); \
} while(0)

// 调试输出宏
#define DEBUG_PRINTF(fmt, ...) do { \
    char _b[512]; \
    int _n = snprintf(_b, sizeof(_b), fmt, ##__VA_ARGS__); \
    if (_n > 0) debug_write(_b, (size_t)_n); \
} while(0)

/* ================================================================
 * JSON 转义
 * ================================================================ */

int json_escape_str(char *dst, const char *src, size_t dst_size) {
    if (!dst || !src || dst_size == 0) return 0;
    size_t written = 0;
    for (const char *p = src; *p && written < dst_size - 1; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') {
            if (written + 2 >= dst_size) break;
            dst[written++] = '\\';
            dst[written++] = '"';
        } else if (c == '\\') {
            if (written + 2 >= dst_size) break;
            dst[written++] = '\\';
            dst[written++] = '\\';
        } else if (c == '\n') {
            if (written + 2 >= dst_size) break;
            dst[written++] = '\\';
            dst[written++] = 'n';
        } else if (c == '\r') {
            if (written + 2 >= dst_size) break;
            dst[written++] = '\\';
            dst[written++] = 'r';
        } else if (c == '\t') {
            if (written + 2 >= dst_size) break;
            dst[written++] = '\\';
            dst[written++] = 't';
        } else if (c < 0x20) {
            if (written + 6 >= dst_size) break;
            int n = snprintf(dst + written, 7, "\\u%04X", c);
            if (n > 0) written += n;
        } else {
            dst[written++] = c;
        }
    }
    dst[written] = '\0';
    return (int)written;
}

/* ================================================================
 * 事件类型名称
 * ================================================================ */

static const char *evt_name(json_event_type_t evt) {
    switch (evt) {
        case JSON_EVT_STARTUP:       return "startup";
        case JSON_EVT_STATUS:        return "status";
        case JSON_EVT_UAV_DISCOVERY: return "uav_discovery";
        case JSON_EVT_UAV_UPDATE:    return "uav_update";
        case JSON_EVT_UAV_STATUS:    return "uav_status";
        case JSON_EVT_UAV_TIMEOUT:   return "uav_timeout";
        case JSON_EVT_UAV_DETAIL:    return "uav_detail";
        case JSON_EVT_WARNING:       return "warning";
        case JSON_EVT_ERROR:         return "error";
        case JSON_EVT_DEBUG:         return "debug";
        case JSON_EVT_DECODE_FAIL:   return "decode_fail";
        case JSON_EVT_IDLE:          return "no_aircraft";
        default:                     return "unknown";
    }
}

/* ================================================================
 * 枚举名称映射（与 crid_display.c 保持一致）
 * ================================================================ */

static const char *s_id_type_name(uint8_t t) {
    switch (t) {
        case ODID_IDTYPE_NONE:                return "none";
        case ODID_IDTYPE_SERIAL_NUMBER:       return "serial_number";
        case ODID_IDTYPE_CAA_REGISTRATION_ID: return "caa_registration_id";
        case ODID_IDTYPE_UTM_ASSIGNED_UUID:   return "utm_assigned_uuid";
        case ODID_IDTYPE_SPECIFIC_SESSION_ID: return "specific_session_id";
        default:                              return "unknown";
    }
}

static const char *s_ua_type_name(uint8_t t) {
    switch (t) {
        case ODID_UATYPE_NONE:                     return "none";
        case ODID_UATYPE_AEROPLANE:                return "aeroplane";
        case ODID_UATYPE_HELICOPTER_OR_MULTIROTOR: return "helicopter_or_multirotor";
        case ODID_UATYPE_GYROPLANE:                return "gyroplane";
        case ODID_UATYPE_HYBRID_LIFT:              return "hybrid_lift";
        case ODID_UATYPE_ORNITHOPTER:              return "ornithopter";
        case ODID_UATYPE_GLIDER:                   return "glider";
        case ODID_UATYPE_KITE:                     return "kite";
        case ODID_UATYPE_FREE_BALLOON:             return "free_balloon";
        case ODID_UATYPE_CAPTIVE_BALLOON:          return "captive_balloon";
        case ODID_UATYPE_AIRSHIP:                  return "airship";
        case ODID_UATYPE_FREE_FALL_PARACHUTE:      return "free_fall_parachute";
        case ODID_UATYPE_ROCKET:                   return "rocket";
        case ODID_UATYPE_TETHERED_POWERED_AIRCRAFT: return "tethered_powered";
        case ODID_UATYPE_GROUND_OBSTACLE:          return "ground_obstacle";
        case ODID_UATYPE_OTHER:                    return "other";
        default:                                   return "unknown";
    }
}

static const char *s_status_name(uint8_t s) {
    switch (s) {
        case ODID_STATUS_UNDECLARED:               return "undeclared";
        case ODID_STATUS_GROUND:                   return "ground";
        case ODID_STATUS_AIRBORNE:                 return "airborne";
        case ODID_STATUS_EMERGENCY:                return "emergency";
        case ODID_STATUS_REMOTE_ID_SYSTEM_FAILURE: return "rid_system_failure";
        default:                                   return "unknown";
    }
}

static const char *s_height_ref_name(uint8_t h) {
    switch (h) {
        case ODID_HEIGHT_REF_OVER_TAKEOFF: return "over_takeoff";
        case ODID_HEIGHT_REF_OVER_GROUND:  return "over_ground";
        default:                           return "unknown";
    }
}

static const char *s_desc_type_name(uint8_t d) {
    switch (d) {
        case ODID_DESC_TYPE_TEXT:            return "text";
        case ODID_DESC_TYPE_EMERGENCY:       return "emergency";
        case ODID_DESC_TYPE_EXTENDED_STATUS: return "extended_status";
        default:                             return "unknown";
    }
}

static const char *s_operator_loc_name(uint8_t t) {
    switch (t) {
        case ODID_OPERATOR_LOCATION_TYPE_TAKEOFF:   return "takeoff";
        case ODID_OPERATOR_LOCATION_TYPE_LIVE_GNSS: return "live_gnss";
        case ODID_OPERATOR_LOCATION_TYPE_FIXED:     return "fixed";
        default:                                    return "unknown";
    }
}

static const char *s_auth_type_name(uint8_t a) {
    switch (a) {
        case ODID_AUTH_NONE:                    return "none";
        case ODID_AUTH_UAS_ID_SIGNATURE:        return "uas_id_signature";
        case ODID_AUTH_OPERATOR_ID_SIGNATURE:   return "operator_id_signature";
        case ODID_AUTH_MESSAGE_SET_SIGNATURE:   return "message_set_signature";
        case ODID_AUTH_NETWORK_REMOTE_ID:       return "network_remote_id";
        case ODID_AUTH_SPECIFIC_AUTHENTICATION: return "specific_authentication";
        default:                                return "unknown";
    }
}

/* ================================================================
 * MAC 地址格式化辅助
 * ================================================================ */

static void mac_to_str(const uint8_t *mac, char *buf, size_t size) {
    snprintf(buf, size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ================================================================
 * JSON 构建函数 — 通用事件
 * ================================================================ */

void json_startup_banner(const char *version, const char *build_date,
                         const char *build_time, uint8_t channel,
                         int max_uavs, uint32_t free_heap) {
    DEBUG_PRINTF("{\"evt\":\"%s\",\"ts\":%lu,\"name\":\"ESP32 Remote ID Scanner\","
           "\"version\":\"%s\",\"build_date\":\"%s\",\"build_time\":\"%s\","
           "\"standards\":[\"ASTM F3411-22a\",\"ASD-STAN prEN 4709-002\",\"GB 42590-2023\",\"GB 46750-2025\"],"
           "\"channel\":%u,\"max_uavs\":%d,\"free_heap\":%lu}\n",
           evt_name(JSON_EVT_STARTUP),
           (unsigned long)esp_log_timestamp(),
           version, build_date, build_time,
           channel, max_uavs, (unsigned long)free_heap);
}

void json_startup_info(const char *version, const char *build_date,
                       const char *build_time, const char *idf_version,
                       uint32_t free_heap, int protocol_version) {
    DEBUG_PRINTF("{\"evt\":\"%s\",\"ts\":%lu,\"version\":\"%s\",\"build\":\"%s %s\","
           "\"idf_version\":\"%s\",\"free_heap\":%lu,\"protocol_version\":%d}\n",
           evt_name(JSON_EVT_STARTUP),
           (unsigned long)esp_log_timestamp(),
           version, build_date, build_time,
           idf_version, (unsigned long)free_heap, protocol_version);
}

void json_status_report(uint32_t loop_minutes, uint32_t free_heap,
                        uint32_t total_pkts, float pkts_per_sec,
                        uint32_t mgmt_frames, float mgmt_per_sec,
                        uint32_t beacons, float beacons_per_sec,
                        uint32_t rid_detections, float rid_per_sec,
                        uint32_t non_rid_vendor, float non_rid_per_sec,
                        uint32_t queue_overflows, int active_uavs) {
    DATA_PRINTF("{\"evt\":\"%s\",\"ts\":%lu,\"loop_min\":%lu,"
           "\"free_heap\":%lu,"
           "\"total_pkts\":%lu,\"pkts_per_sec\":%.1f,"
           "\"mgmt_frames\":%lu,\"mgmt_per_sec\":%.1f,"
           "\"beacons\":%lu,\"beacons_per_sec\":%.1f,"
           "\"rid_detections\":%lu,\"rid_per_sec\":%.1f,"
           "\"non_rid_vendor\":%lu,\"non_rid_per_sec\":%.1f,"
           "\"queue_overflows\":%lu,\"active_uavs\":%d}\n",
           evt_name(JSON_EVT_STATUS),
           (unsigned long)esp_log_timestamp(),
           (unsigned long)loop_minutes,
           (unsigned long)free_heap,
           (unsigned long)total_pkts, pkts_per_sec,
           (unsigned long)mgmt_frames, mgmt_per_sec,
           (unsigned long)beacons, beacons_per_sec,
           (unsigned long)rid_detections, rid_per_sec,
           (unsigned long)non_rid_vendor, non_rid_per_sec,
            (unsigned long)queue_overflows, active_uavs);
    DATA_PRINTF("[ZH]状态|%lumin|%d架|%.1fpkt/s|heap:%lu[/ZH]\n",
            (unsigned long)loop_minutes, active_uavs, pkts_per_sec,
            (unsigned long)free_heap);
}

/* ================================================================
 * JSON 构建函数 — UAV 核心数据（内部辅助，输出到指定流）
 * ================================================================ */

// 构建一个 JSON 片段的辅助宏
#define BPRINTF(_which, fmt, ...) do { \
    char _b[512]; \
    int _n = snprintf(_b, sizeof(_b), fmt, ##__VA_ARGS__); \
    if (_n > 0) _which ## _write(_b, (size_t)_n); \
} while(0)

#define DBG_PRINTF(fmt, ...) BPRINTF(debug, fmt, ##__VA_ARGS__)
#define DAT_PRINTF(fmt, ...) BPRINTF(data,  fmt, ##__VA_ARGS__)

/**
 * 输出 UAV 通用头部字段（MAC, RSSI, transport, protocol 等）
 * @param which  "data" 或 "debug"
 */
static void json_uav_header_impl(const uav_track_t *uav, json_event_type_t evt,
                                  bool is_data) {
    char mac_str[18];
    mac_to_str(uav->mac, mac_str, sizeof(mac_str));

    if (is_data) {
        DAT_PRINTF("{\"evt\":\"%s\",\"ts\":%lu,"
               "\"mac\":\"%s\",\"rssi\":%d,\"channel\":%u,"
               "\"transport\":\"%s\",\"protocol\":\"%s\","
               "\"msg_count\":%lu",
               evt_name(evt), (unsigned long)esp_log_timestamp(),
               mac_str, uav->last_rssi, uav->last_channel,
               crid_display_transport_name(uav->transport),
               crid_display_protocol_name(uav->protocol),
               (unsigned long)uav->msg_count);
    } else {
        DBG_PRINTF("{\"evt\":\"%s\",\"ts\":%lu,"
               "\"mac\":\"%s\",\"rssi\":%d,\"channel\":%u,"
               "\"transport\":\"%s\",\"protocol\":\"%s\","
               "\"msg_count\":%lu",
               evt_name(evt), (unsigned long)esp_log_timestamp(),
               mac_str, uav->last_rssi, uav->last_channel,
               crid_display_transport_name(uav->transport),
               crid_display_protocol_name(uav->protocol),
               (unsigned long)uav->msg_count);
    }
}

static void json_uav_basic_id_impl(const uav_track_t *uav, bool is_data) {
    if (uav->basic_id.valid) {
        char escaped[25];
        json_escape_str(escaped, uav->basic_id.uas_id, sizeof(escaped));
        if (is_data) {
            DAT_PRINTF(",\"basic_id\":{\"id_type\":\"%s\",\"ua_type\":\"%s\",\"uas_id\":\"%s\"}",
                   s_id_type_name(uav->basic_id.id_type),
                   s_ua_type_name(uav->basic_id.ua_type), escaped);
        } else {
            DBG_PRINTF(",\"basic_id\":{\"id_type\":\"%s\",\"ua_type\":\"%s\",\"uas_id\":\"%s\"}",
                   s_id_type_name(uav->basic_id.id_type),
                   s_ua_type_name(uav->basic_id.ua_type), escaped);
        }
    } else {
        if (is_data) DAT_PRINTF(",\"basic_id\":null");
        else         DBG_PRINTF(",\"basic_id\":null");
    }
}

static void json_uav_location_impl(const uav_track_t *uav, bool is_data) {
    if (uav->location.valid) {
        if (is_data) {
            DAT_PRINTF(",\"location\":{"
                   "\"status\":\"%s\","
                   "\"latitude\":%.7f,\"longitude\":%.7f,"
                   "\"alt_baro\":%.1f,\"alt_geo\":%.1f,"
                   "\"height\":%.1f,\"height_ref\":\"%s\","
                   "\"direction\":%.1f,"
                   "\"speed_h\":%.2f,\"speed_v\":%.2f,"
                   "\"timestamp\":%.1f,\"ts_acc\":%u}",
                   s_status_name(uav->location.status),
                   uav->location.latitude, uav->location.longitude,
                   uav->location.altitude_baro, uav->location.altitude_geo,
                   uav->location.height, s_height_ref_name(uav->location.height_ref),
                   uav->location.direction,
                   uav->location.speed_horizontal, uav->location.speed_vertical,
                   uav->location.timestamp, uav->location.ts_accuracy);
        } else {
            DBG_PRINTF(",\"location\":{"
                   "\"status\":\"%s\","
                   "\"latitude\":%.7f,\"longitude\":%.7f,"
                   "\"alt_baro\":%.1f,\"alt_geo\":%.1f,"
                   "\"height\":%.1f,\"height_ref\":\"%s\","
                   "\"direction\":%.1f,"
                   "\"speed_h\":%.2f,\"speed_v\":%.2f,"
                   "\"timestamp\":%.1f,\"ts_acc\":%u}",
                   s_status_name(uav->location.status),
                   uav->location.latitude, uav->location.longitude,
                   uav->location.altitude_baro, uav->location.altitude_geo,
                   uav->location.height, s_height_ref_name(uav->location.height_ref),
                   uav->location.direction,
                   uav->location.speed_horizontal, uav->location.speed_vertical,
                   uav->location.timestamp, uav->location.ts_accuracy);
        }
    } else {
        if (is_data) DAT_PRINTF(",\"location\":null");
        else         DBG_PRINTF(",\"location\":null");
    }
}

static void json_uav_location_compact_impl(const uav_track_t *uav, bool is_data) {
    if (uav->location.valid) {
        if (is_data) {
            DAT_PRINTF(",\"lat\":%.7f,\"lon\":%.7f,\"alt_baro\":%.1f,\"alt_geo\":%.1f,"
                   "\"height\":%.1f,\"speed_h\":%.2f,\"speed_v\":%.2f,"
                   "\"direction\":%.1f,\"status\":\"%s\"",
                   uav->location.latitude, uav->location.longitude,
                   uav->location.altitude_baro, uav->location.altitude_geo,
                   uav->location.height,
                   uav->location.speed_horizontal, uav->location.speed_vertical,
                   uav->location.direction, s_status_name(uav->location.status));
        } else {
            DBG_PRINTF(",\"lat\":%.7f,\"lon\":%.7f,\"alt_baro\":%.1f,\"alt_geo\":%.1f,"
                   "\"height\":%.1f,\"speed_h\":%.2f,\"speed_v\":%.2f,"
                   "\"direction\":%.1f,\"status\":\"%s\"",
                   uav->location.latitude, uav->location.longitude,
                   uav->location.altitude_baro, uav->location.altitude_geo,
                   uav->location.height,
                   uav->location.speed_horizontal, uav->location.speed_vertical,
                   uav->location.direction, s_status_name(uav->location.status));
        }
    }
}

static void json_uav_system_impl(const uav_track_t *uav, bool is_data) {
    if (uav->system.valid) {
        if (is_data) {
            DAT_PRINTF(",\"system\":{"
                   "\"operator_loc_type\":\"%s\","
                   "\"operator_lat\":%.7f,\"operator_lon\":%.7f,"
                   "\"operator_alt_geo\":%.1f,"
                   "\"area_count\":%u,\"area_radius\":%u,"
                   "\"area_ceiling\":%.1f,\"area_floor\":%.1f,"
                   "\"classification\":%u,\"category_eu\":%u,\"class_eu\":%u,"
                   "\"timestamp\":%lu}",
                   s_operator_loc_name(uav->system.operator_location_type),
                   uav->system.operator_latitude, uav->system.operator_longitude,
                   uav->system.operator_altitude_geo,
                   uav->system.area_count, uav->system.area_radius,
                   uav->system.area_ceiling, uav->system.area_floor,
                   uav->system.classification_type,
                   uav->system.category_eu, uav->system.class_eu,
                   (unsigned long)uav->system.timestamp);
        } else {
            DBG_PRINTF(",\"system\":{"
                   "\"operator_loc_type\":\"%s\","
                   "\"operator_lat\":%.7f,\"operator_lon\":%.7f,"
                   "\"operator_alt_geo\":%.1f,"
                   "\"area_count\":%u,\"area_radius\":%u,"
                   "\"area_ceiling\":%.1f,\"area_floor\":%.1f,"
                   "\"classification\":%u,\"category_eu\":%u,\"class_eu\":%u,"
                   "\"timestamp\":%lu}",
                   s_operator_loc_name(uav->system.operator_location_type),
                   uav->system.operator_latitude, uav->system.operator_longitude,
                   uav->system.operator_altitude_geo,
                   uav->system.area_count, uav->system.area_radius,
                   uav->system.area_ceiling, uav->system.area_floor,
                   uav->system.classification_type,
                   uav->system.category_eu, uav->system.class_eu,
                   (unsigned long)uav->system.timestamp);
        }
    } else {
        if (is_data) DAT_PRINTF(",\"system\":null");
        else         DBG_PRINTF(",\"system\":null");
    }
}

static void json_uav_self_id_impl(const uav_track_t *uav, bool is_data) {
    if (uav->self_id.valid) {
        char escaped[30];
        json_escape_str(escaped, uav->self_id.description, sizeof(escaped));
        if (is_data) {
            DAT_PRINTF(",\"self_id\":{\"type\":\"%s\",\"desc\":\"%s\"}",
                   s_desc_type_name(uav->self_id.description_type), escaped);
        } else {
            DBG_PRINTF(",\"self_id\":{\"type\":\"%s\",\"desc\":\"%s\"}",
                   s_desc_type_name(uav->self_id.description_type), escaped);
        }
    } else {
        if (is_data) DAT_PRINTF(",\"self_id\":null");
        else         DBG_PRINTF(",\"self_id\":null");
    }
}

static void json_uav_operator_id_impl(const uav_track_t *uav, bool is_data) {
    if (uav->operator_id.valid) {
        char escaped[25];
        json_escape_str(escaped, uav->operator_id.id, sizeof(escaped));
        if (is_data) {
            DAT_PRINTF(",\"operator_id\":{\"type\":%u,\"id\":\"%s\"}",
                   uav->operator_id.id_type, escaped);
        } else {
            DBG_PRINTF(",\"operator_id\":{\"type\":%u,\"id\":\"%s\"}",
                   uav->operator_id.id_type, escaped);
        }
    } else {
        if (is_data) DAT_PRINTF(",\"operator_id\":null");
        else         DBG_PRINTF(",\"operator_id\":null");
    }
}

static void json_uav_auth_impl(const uav_track_t *uav, bool is_data) {
    int auth_count = 0;
    for (int i = 0; i < ODID_AUTH_MAX_PAGES; i++) {
        if (uav->uas_data.AuthValid[i]) auth_count++;
    }
    if (auth_count == 0) {
        if (is_data) DAT_PRINTF(",\"auth\":[]");
        else         DBG_PRINTF(",\"auth\":[]");
        return;
    }

    if (is_data) DAT_PRINTF(",\"auth\":[");
    else         DBG_PRINTF(",\"auth\":[");

    bool first = true;
    for (int i = 0; i < ODID_AUTH_MAX_PAGES; i++) {
        if (!uav->uas_data.AuthValid[i]) continue;
        const ODID_Auth_data *a = &uav->uas_data.Auth[i];
        if (!first) { if (is_data) DAT_PRINTF(","); else DBG_PRINTF(","); }
        if (is_data) {
            DAT_PRINTF("{\"page\":%u,\"type\":\"%s\"", a->DataPage, s_auth_type_name((uint8_t)a->AuthType));
            if (a->DataPage == 0) {
                DAT_PRINTF(",\"last_page\":%u,\"length\":%u", a->LastPageIndex, a->Length);
            }
            DAT_PRINTF("}");
        } else {
            DBG_PRINTF("{\"page\":%u,\"type\":\"%s\"", a->DataPage, s_auth_type_name((uint8_t)a->AuthType));
            if (a->DataPage == 0) {
                DBG_PRINTF(",\"last_page\":%u,\"length\":%u", a->LastPageIndex, a->Length);
            }
            DBG_PRINTF("}");
        }
        first = false;
    }
    if (is_data) DAT_PRINTF("]");
    else         DBG_PRINTF("]");
}

static void json_uav_gb46750_impl(const uav_track_t *uav, bool is_data) {
    if (!uav->gb46750.valid) {
        if (is_data) DAT_PRINTF(",\"gb46750\":null");
        else         DBG_PRINTF(",\"gb46750\":null");
        return;
    }
    const gb46750_data_t *gb = &uav->gb46750;
    if (is_data) DAT_PRINTF(",\"gb46750\":{");
    else         DBG_PRINTF(",\"gb46750\":{");

    bool first = true;
#define GB_FIELD_INT(name, cond, val) do { \
    if (cond) { \
        if (!first) { if (is_data) DAT_PRINTF(","); else DBG_PRINTF(","); } \
        if (is_data) DAT_PRINTF("\"" name "\":%u", (unsigned)(val)); \
        else         DBG_PRINTF("\"" name "\":%u", (unsigned)(val)); \
        first = false; \
    } \
} while(0)

#define GB_FIELD_FLOAT(name, cond, val) do { \
    if (cond) { \
        if (!first) { if (is_data) DAT_PRINTF(","); else DBG_PRINTF(","); } \
        if (is_data) DAT_PRINTF("\"" name "\":%.7f", (double)(val)); \
        else         DBG_PRINTF("\"" name "\":%.7f", (double)(val)); \
        first = false; \
    } \
} while(0)

#define GB_FIELD_STR(name, cond, val) do { \
    if (cond) { \
        char _esc[30]; \
        json_escape_str(_esc, val, sizeof(_esc)); \
        if (!first) { if (is_data) DAT_PRINTF(","); else DBG_PRINTF(","); } \
        if (is_data) DAT_PRINTF("\"" name "\":\"%s\"", _esc); \
        else         DBG_PRINTF("\"" name "\":\"%s\"", _esc); \
        first = false; \
    } \
} while(0)

#define GB_FIELD_U64(name, cond, val) do { \
    if (cond) { \
        if (!first) { if (is_data) DAT_PRINTF(","); else DBG_PRINTF(","); } \
        if (is_data) DAT_PRINTF("\"" name "\":%llu", (unsigned long long)(val)); \
        else         DBG_PRINTF("\"" name "\":%llu", (unsigned long long)(val)); \
        first = false; \
    } \
} while(0)

    GB_FIELD_STR("unique_id", gb->has_unique_id, gb->unique_id);
    GB_FIELD_STR("realname_id", gb->has_realname_flag, gb->realname_id);
    GB_FIELD_INT("operation_category", gb->has_operation_category, gb->operation_category);
    GB_FIELD_INT("ua_category", gb->has_ua_category, gb->ua_category);
    GB_FIELD_INT("rcs_loc_type", gb->has_rcs_loc_type, gb->rcs_loc_type);
    GB_FIELD_FLOAT("rcs_latitude", gb->has_rcs_location, gb->rcs_latitude);
    GB_FIELD_FLOAT("rcs_longitude", gb->has_rcs_location, gb->rcs_longitude);
    GB_FIELD_FLOAT("rcs_altitude", gb->has_rcs_altitude, gb->rcs_altitude);
    GB_FIELD_FLOAT("uav_latitude", gb->has_uav_location, gb->uav_latitude);
    GB_FIELD_FLOAT("uav_longitude", gb->has_uav_location, gb->uav_longitude);
    GB_FIELD_FLOAT("track_angle", gb->has_track_angle, gb->track_angle);
    GB_FIELD_FLOAT("ground_speed", gb->has_ground_speed, gb->ground_speed);
    GB_FIELD_FLOAT("relative_height", gb->has_relative_height, gb->relative_height);
    GB_FIELD_FLOAT("vertical_speed", gb->has_vertical_speed, gb->vertical_speed);
    GB_FIELD_FLOAT("geo_altitude", gb->has_geo_altitude, gb->geo_altitude);
    GB_FIELD_FLOAT("baro_altitude", gb->has_baro_altitude, gb->baro_altitude);
    GB_FIELD_INT("operation_status", gb->has_operation_status, gb->operation_status);
    GB_FIELD_INT("coord_system", gb->has_coord_system, gb->coord_system);
    GB_FIELD_INT("h_accuracy", gb->has_h_accuracy, gb->h_accuracy);
    GB_FIELD_INT("v_accuracy", gb->has_v_accuracy, gb->v_accuracy);
    GB_FIELD_INT("speed_accuracy", gb->has_speed_accuracy, gb->speed_accuracy);
    GB_FIELD_U64("timestamp_ms", gb->has_timestamp, gb->timestamp_ms);
    GB_FIELD_INT("ts_accuracy", gb->has_ts_accuracy, gb->ts_accuracy);

#undef GB_FIELD_INT
#undef GB_FIELD_FLOAT
#undef GB_FIELD_STR
#undef GB_FIELD_U64

    if (is_data) DAT_PRINTF("}");
    else         DBG_PRINTF("}");
}

static void json_uav_basic_id_all_impl(const uav_track_t *uav, bool is_data) {
    if (is_data) DAT_PRINTF(",\"basic_ids\":[");
    else         DBG_PRINTF(",\"basic_ids\":[");

    bool first = true;
    for (int i = 0; i < ODID_BASIC_ID_MAX_MESSAGES; i++) {
        if (!uav->uas_data.BasicIDValid[i]) continue;
        const ODID_BasicID_data *b = &uav->uas_data.BasicID[i];
        char escaped[25];
        json_escape_str(escaped, b->UASID, sizeof(escaped));
        if (!first) { if (is_data) DAT_PRINTF(","); else DBG_PRINTF(","); }
        if (is_data) {
            DAT_PRINTF("{\"index\":%d,\"id_type\":\"%s\",\"ua_type\":\"%s\",\"uas_id\":\"%s\"}",
                   i, s_id_type_name((uint8_t)b->IDType),
                   s_ua_type_name((uint8_t)b->UAType), escaped);
        } else {
            DBG_PRINTF("{\"index\":%d,\"id_type\":\"%s\",\"ua_type\":\"%s\",\"uas_id\":\"%s\"}",
                   i, s_id_type_name((uint8_t)b->IDType),
                   s_ua_type_name((uint8_t)b->UAType), escaped);
        }
        first = false;
    }
    if (is_data) DAT_PRINTF("]");
    else         DBG_PRINTF("]");
}

/* ================================================================
 * JSON 构建函数 — UAV 公开接口（全部输出到 data 流）
 * ================================================================ */

void json_uav_discovery(const uav_track_t *uav) {
    json_uav_header_impl(uav, JSON_EVT_UAV_DISCOVERY, true);
    json_uav_basic_id_impl(uav, true);
    json_uav_location_compact_impl(uav, true);
    DAT_PRINTF("}\n");
    if (uav->basic_id.valid) {
        DAT_PRINTF("[ZH]发现无人机|%s|RSSI:%d|信道:%d[/ZH]\n",
               uav->basic_id.uas_id, uav->last_rssi, uav->last_channel);
    }
}

void json_uav_update(const uav_track_t *uav) {
    json_uav_header_impl(uav, JSON_EVT_UAV_UPDATE, true);
    json_uav_basic_id_impl(uav, true);
    json_uav_location_impl(uav, true);
    json_uav_system_impl(uav, true);
    json_uav_self_id_impl(uav, true);
    json_uav_operator_id_impl(uav, true);
    json_uav_auth_impl(uav, true);
    json_uav_gb46750_impl(uav, true);
    DAT_PRINTF("}\n");
    DAT_PRINTF("[ZH]无人机|%s|%s|%.5f,%.5f|%.0fm|%.1fkm/h|%d°|高度%.0fm|区域:%d个半径%dm",
           uav->basic_id.valid ? uav->basic_id.uas_id : "?",
           s_status_name(uav->location.status),
           uav->location.latitude, uav->location.longitude,
           uav->location.altitude_baro,
           uav->location.speed_horizontal * 3.6,
           (int)uav->location.direction,
           uav->location.height,
           uav->system.area_count, uav->system.area_radius);
    if (uav->system.operator_location_type == ODID_OPERATOR_LOCATION_TYPE_TAKEOFF) {
        DAT_PRINTF("|操作员:%.5f,%.5f(起飞点)", uav->system.operator_latitude, uav->system.operator_longitude);
    } else if (uav->system.operator_location_type == ODID_OPERATOR_LOCATION_TYPE_LIVE_GNSS) {
        DAT_PRINTF("|操作员:%.5f,%.5f(实时)", uav->system.operator_latitude, uav->system.operator_longitude);
    } else if (uav->system.operator_location_type == ODID_OPERATOR_LOCATION_TYPE_FIXED) {
        DAT_PRINTF("|操作员:%.5f,%.5f(固定)", uav->system.operator_latitude, uav->system.operator_longitude);
    }
    DAT_PRINTF("|自ID:%s", uav->self_id.valid ? uav->self_id.description : "无");
    DAT_PRINTF("|操作ID:%s", uav->operator_id.valid ? uav->operator_id.id : "无");
    int auth_count = 0;
    for (int i = 0; i < ODID_AUTH_MAX_PAGES; i++) {
        if (uav->uas_data.AuthValid[i]) auth_count++;
    }
    DAT_PRINTF("|认证:%d条", auth_count);
    if (uav->gb46750.valid) {
        DAT_PRINTF("|实名:%s|GB46750:是",
               uav->gb46750.has_realname_flag ? uav->gb46750.realname_id : "无");
    } else {
        DAT_PRINTF("|实名:无|GB46750:否");
    }
    DAT_PRINTF("|RSSI:%d|信道:%d[/ZH]\n", uav->last_rssi, uav->last_channel);
}

void json_uav_status(const uav_track_t *uav) {
    uint32_t age_ms = esp_log_timestamp() - uav->last_seen_ms;

    json_uav_header_impl(uav, JSON_EVT_UAV_STATUS, true);
    DAT_PRINTF(",\"age_ms\":%lu", (unsigned long)age_ms);
    json_uav_basic_id_impl(uav, true);
    json_uav_location_compact_impl(uav, true);
    DAT_PRINTF("}\n");
    if (uav->basic_id.valid) {
        DAT_PRINTF("[ZH]UAV状态|%s|age:%lus|RSSI:%d|信道:%d[/ZH]\n",
               uav->basic_id.uas_id, (unsigned long)(age_ms / 1000),
               uav->last_rssi, uav->last_channel);
    }
}

void json_uav_detail(const uav_track_t *uav) {
    uint32_t age_ms = esp_log_timestamp() - uav->last_seen_ms;

    json_uav_header_impl(uav, JSON_EVT_UAV_DETAIL, true);
    DAT_PRINTF(",\"age_ms\":%lu,\"first_seen_ms\":%lu",
           (unsigned long)age_ms, (unsigned long)uav->first_seen_ms);
    json_uav_basic_id_all_impl(uav, true);
    json_uav_location_impl(uav, true);
    json_uav_system_impl(uav, true);
    json_uav_self_id_impl(uav, true);
    json_uav_operator_id_impl(uav, true);
    json_uav_auth_impl(uav, true);
    json_uav_gb46750_impl(uav, true);
    DAT_PRINTF("}\n");
}

void json_uav_timeout(const uint8_t *mac) {
    char mac_str[18];
    mac_to_str(mac, mac_str, sizeof(mac_str));

    DAT_PRINTF("{\"evt\":\"%s\",\"ts\":%lu,\"mac\":\"%s\"}\n",
           evt_name(JSON_EVT_UAV_TIMEOUT),
           (unsigned long)esp_log_timestamp(), mac_str);
    DAT_PRINTF("[ZH]UAV超时|%s[/ZH]\n", mac_str);
}

void json_no_aircraft(void) {
    DAT_PRINTF("{\"evt\":\"%s\",\"ts\":%lu}\n",
           evt_name(JSON_EVT_IDLE),
           (unsigned long)esp_log_timestamp());
    DAT_PRINTF("[ZH]无新飞机[/ZH]\n");
}

/* ================================================================
 * JSON 构建函数 — 日志/诊断（全部输出到 debug 流）
 * ================================================================ */

void json_warning(const char *module, const char *message) {
    char escaped[256];
    json_escape_str(escaped, message, sizeof(escaped));
    DEBUG_PRINTF("{\"evt\":\"%s\",\"ts\":%lu,\"module\":\"%s\",\"msg\":\"%s\"}\n",
           evt_name(JSON_EVT_WARNING),
           (unsigned long)esp_log_timestamp(), module, escaped);
}

void json_error(const char *module, const char *message) {
    char escaped[256];
    json_escape_str(escaped, message, sizeof(escaped));
    DEBUG_PRINTF("{\"evt\":\"%s\",\"ts\":%lu,\"module\":\"%s\",\"msg\":\"%s\"}\n",
           evt_name(JSON_EVT_ERROR),
           (unsigned long)esp_log_timestamp(), module, escaped);
}

void json_debug(const char *module, const char *message) {
    char escaped[256];
    json_escape_str(escaped, message, sizeof(escaped));
    DEBUG_PRINTF("{\"evt\":\"%s\",\"ts\":%lu,\"module\":\"%s\",\"msg\":\"%s\"}\n",
           evt_name(JSON_EVT_DEBUG),
           (unsigned long)esp_log_timestamp(), module, escaped);
}

void json_decode_fail(uint8_t byte0, uint8_t byte1, uint8_t len) {
    DEBUG_PRINTF("{\"evt\":\"%s\",\"ts\":%lu,\"byte0\":\"0x%02X\",\"byte1\":\"0x%02X\",\"data_len\":%u}\n",
           evt_name(JSON_EVT_DECODE_FAIL),
           (unsigned long)esp_log_timestamp(),
           byte0, byte1, len);
}
