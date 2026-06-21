/**
 * crid_display.c — 无人机信息展示模块
 *
 * 包含：
 *   - 枚举值 → 可读名称映射表 (基于分层结构体 rid_*_t)
 *   - MAC 地址格式化
 *   - 摘要/详情/状态行打印
 *
 * 设计：使用 rid_*_t 分层结构体（与 ODID_UAS_Data 解耦），
 *       所有枚举值通过 uint8_t 传递，映射表参照 ASTM F3411 / ORIP 标准。
 *       共享枚举映射由 crid_enum_names.h (X-macro) 统一生成。
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <esp_log.h>
#include "crid_display.h"
#include "crid_enum_names.h"
#include "opendroneid.h"  // 仅用于 decodeTimestampAccuracy 和 ODID_AUTH_MAX_PAGES 等常量

static const char *TAG = "RID_DISP";

/* ================================================================
 * 共享枚举 → 可读名称映射 (由 crid_enum_names.h X-macro 生成)
 * ================================================================ */

ENUM_DISPLAY_FN(get_id_type_name,        ID_TYPE_MAP,    "Unknown")
ENUM_DISPLAY_FN(get_ua_type_name,        UA_TYPE_MAP,    "Unknown")
ENUM_DISPLAY_FN(get_status_name,         STATUS_MAP,     "Unknown")
ENUM_DISPLAY_FN(get_height_ref_name,     HEIGHT_REF_MAP, "Unknown")
ENUM_DISPLAY_FN(get_desc_type_name,      DESC_TYPE_MAP,  "Unknown")
ENUM_DISPLAY_FN(get_operator_loc_name,   OPERATOR_LOC_MAP, "Unknown")
ENUM_DISPLAY_FN(get_auth_type_name,      AUTH_TYPE_MAP,  "Unknown")

/* ================================================================
 * 显示专用枚举映射 (仅在 display 层使用)
 * ================================================================ */

static const char *get_horiz_acc_name(uint8_t a) {
    switch (a) {
        case ODID_HOR_ACC_UNKNOWN:  return "Unknown";
        case ODID_HOR_ACC_10NM:     return "18.52 km";
        case ODID_HOR_ACC_4NM:      return "7.408 km";
        case ODID_HOR_ACC_2NM:      return "3.704 km";
        case ODID_HOR_ACC_1NM:      return "1.852 km";
        case ODID_HOR_ACC_0_5NM:    return "926 m";
        case ODID_HOR_ACC_0_3NM:    return "555.6 m";
        case ODID_HOR_ACC_0_1NM:    return "185.2 m";
        case ODID_HOR_ACC_0_05NM:   return "92.6 m";
        case ODID_HOR_ACC_30_METER: return "30 m";
        case ODID_HOR_ACC_10_METER: return "10 m";
        case ODID_HOR_ACC_3_METER:  return "3 m";
        case ODID_HOR_ACC_1_METER:  return "1 m";
        default:                    return "Unknown";
    }
}

static const char *get_vert_acc_name(uint8_t a) {
    switch (a) {
        case ODID_VER_ACC_UNKNOWN:   return "Unknown";
        case ODID_VER_ACC_150_METER: return "150 m";
        case ODID_VER_ACC_45_METER:  return "45 m";
        case ODID_VER_ACC_25_METER:  return "25 m";
        case ODID_VER_ACC_10_METER:  return "10 m";
        case ODID_VER_ACC_3_METER:   return "3 m";
        case ODID_VER_ACC_1_METER:   return "1 m";
        default:                     return "Unknown";
    }
}

static const char *get_speed_acc_name(uint8_t a) {
    switch (a) {
        case ODID_SPEED_ACC_UNKNOWN:                   return "Unknown";
        case ODID_SPEED_ACC_10_METERS_PER_SECOND:      return "10 m/s";
        case ODID_SPEED_ACC_3_METERS_PER_SECOND:       return "3 m/s";
        case ODID_SPEED_ACC_1_METERS_PER_SECOND:       return "1 m/s";
        case ODID_SPEED_ACC_0_3_METERS_PER_SECOND:     return "0.3 m/s";
        default:                                       return "Unknown";
    }
}

static const char *get_classification_name(uint8_t c) {
    switch (c) {
        case ODID_CLASSIFICATION_TYPE_UNDECLARED: return "Undeclared";
        case ODID_CLASSIFICATION_TYPE_EU:         return "EU";
        default:                                  return "Unknown";
    }
}

static const char *get_eu_category_name(uint8_t c) {
    switch (c) {
        case ODID_CATEGORY_EU_UNDECLARED: return "Undeclared";
        case ODID_CATEGORY_EU_OPEN:       return "Open";
        case ODID_CATEGORY_EU_SPECIFIC:   return "Specific";
        case ODID_CATEGORY_EU_CERTIFIED:  return "Certified";
        default:                          return "Unknown";
    }
}

static const char *get_eu_class_name(uint8_t c) {
    switch (c) {
        case ODID_CLASS_EU_UNDECLARED: return "Undeclared";
        case ODID_CLASS_EU_CLASS_0:    return "Class 0";
        case ODID_CLASS_EU_CLASS_1:    return "Class 1";
        case ODID_CLASS_EU_CLASS_2:    return "Class 2";
        case ODID_CLASS_EU_CLASS_3:    return "Class 3";
        case ODID_CLASS_EU_CLASS_4:    return "Class 4";
        case ODID_CLASS_EU_CLASS_5:    return "Class 5";
        case ODID_CLASS_EU_CLASS_6:    return "Class 6";
        default:                       return "Unknown";
    }
}

static const char *get_transport_name(uint8_t t) {
    switch (t) {
        case RID_TRANSPORT_BLUETOOTH_LEGACY:     return "BT Legacy";
        case RID_TRANSPORT_BLUETOOTH_LONG_RANGE: return "BT Long Range";
        case RID_TRANSPORT_WIFI_NAN:             return "Wi-Fi NAN";
        case RID_TRANSPORT_WIFI_BEACON:          return "Wi-Fi Beacon";
        default:                                  return "Unknown";
    }
}

static const char *get_protocol_name(uint8_t p) {
    switch (p) {
        case RID_PROTOCOL_ASTM_F3411: return "ASTM F3411";
        case RID_PROTOCOL_ASD_STAN:   return "ASD-STAN";
        case RID_PROTOCOL_GB42590:    return "GB 42590";
        case RID_PROTOCOL_GB46750:    return "GB 46750";
        default:                      return "Unknown";
    }
}

/* ================================================================
 * 辅助函数：格式化浮点数输出，避免无意义的小数部分
 * ================================================================ */
static void format_float_buffer(char *buffer, size_t buffer_size, float value, int decimal_places) {
    if (decimal_places == 0) {
        snprintf(buffer, buffer_size, "%.0f", value);
    } else {
        snprintf(buffer, buffer_size, "%.*f", decimal_places, value);
    }
}

/* ================================================================
 * 辅助函数：获取位置坐标字符串
 * ================================================================ */
static void format_position_buffer(char *buffer, size_t buffer_size, double value, int decimal_places) {
    if (decimal_places == 0) {
        snprintf(buffer, buffer_size, "%.0f", value);
    } else {
        snprintf(buffer, buffer_size, "%.*f", decimal_places, value);
    }
}

/* ================================================================
 * 公开名称映射函数 (供外部模块如 crid_scan_main.c 使用)
 * ================================================================ */

const char *crid_display_transport_name(uint8_t t) {
    return get_transport_name(t);
}

const char *crid_display_protocol_name(uint8_t p) {
    return get_protocol_name(p);
}

const char *crid_display_ua_type_name(uint8_t t) {
    return get_ua_type_name(t);
}

const char *crid_display_status_name(uint8_t s) {
    return get_status_name(s);
}

/* ================================================================
 * MAC 地址格式化
 * ================================================================ */

void crid_display_mac_str(const uint8_t *mac, char *buf, size_t size) {
    if (!mac || !buf || size < 18) {
        if (buf && size > 0) {
            buf[0] = '\0';
        }
        return;
    }

    snprintf(buf, size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ================================================================
 * 精简发现打印（仅 1 行，新 UAV 首次发现时调用）
 * 格式: [MAC] ID @ lat,lng alt=Xm spd=X.Xm/s RSSI=-XXdBm
 * ================================================================ */

void crid_display_uav_summary(const uav_track_t *uav) {
    if (!uav) {
        ESP_LOGW(TAG, "Invalid UAV pointer in summary");
        return;
    }

    char mac_str[18];
    crid_display_mac_str(uav->mac, mac_str, sizeof(mac_str));

    if (uav->basic_id.valid && uav->location.valid) {
        char lat_buf[20], lon_buf[20], alt_buf[10], spd_buf[10];

        format_position_buffer(lat_buf, sizeof(lat_buf), uav->location.latitude, 5);
        format_position_buffer(lon_buf, sizeof(lon_buf), uav->location.longitude, 5);
        format_float_buffer(alt_buf, sizeof(alt_buf), uav->location.altitude_baro, 0);
        format_float_buffer(spd_buf, sizeof(spd_buf), uav->location.speed_horizontal, 1);

        ESP_LOGI(TAG, "[%s] %s @ %s,%s alt=%sm spd=%s m/s rssi=%d",
                 mac_str, uav->basic_id.uas_id,
                 lat_buf, lon_buf,
                 alt_buf, spd_buf, uav->last_rssi);
    } else if (uav->basic_id.valid) {
        ESP_LOGI(TAG, "[%s] %s (no pos) rssi=%d",
                 mac_str, uav->basic_id.uas_id, uav->last_rssi);
    } else {
        ESP_LOGI(TAG, "[%s] (no ID) rssi=%d", mac_str, uav->last_rssi);
    }
}

/* ================================================================
 * 详细信息打印（所有字段）
 * 使用 rid_*_t 分层结构体
 * ================================================================ */

static void print_basic_id(const uav_track_t *uav) {
    if (!uav) return;

    if (uav->basic_id.valid) {
        ESP_LOGI(TAG, "  Basic ID:");
        ESP_LOGI(TAG, "    ID Type: %s", get_id_type_name(uav->basic_id.id_type));
        ESP_LOGI(TAG, "    UA Type: %s", get_ua_type_name(uav->basic_id.ua_type));
        ESP_LOGI(TAG, "    UAS ID:  '%s'", uav->basic_id.uas_id);
    }

    /* GB 46750 协议没有 ASTM 多消息 Basic ID，跳过 */
    if (uav->protocol == RID_PROTOCOL_GB46750 && uav->gb46750.valid) return;

    for (int i = 1; i < ODID_BASIC_ID_MAX_MESSAGES; i++) {
        if (!uav->uas_data.BasicIDValid[i]) continue;
        const ODID_BasicID_data *b = &uav->uas_data.BasicID[i];
        ESP_LOGI(TAG, "  Basic ID[%d]:", i);
        ESP_LOGI(TAG, "    ID Type: %s", get_id_type_name((uint8_t)b->IDType));
        ESP_LOGI(TAG, "    UA Type: %s", get_ua_type_name((uint8_t)b->UAType));
        ESP_LOGI(TAG, "    UAS ID:  '%s'", b->UASID);
    }
}

static void print_location(const uav_track_t *uav) {
    if (!uav || !uav->location.valid) return;

    char speed_h_buf[10], speed_v_buf[10], lat_buf[20], lon_buf[20];

    format_float_buffer(speed_h_buf, sizeof(speed_h_buf), uav->location.speed_horizontal, 2);
    format_float_buffer(speed_v_buf, sizeof(speed_v_buf), uav->location.speed_vertical, 2);
    format_position_buffer(lat_buf, sizeof(lat_buf), uav->location.latitude, 7);
    format_position_buffer(lon_buf, sizeof(lon_buf), uav->location.longitude, 7);

    ESP_LOGI(TAG, "  Location:");
    ESP_LOGI(TAG, "    Status:      %s", get_status_name(uav->location.status));
    ESP_LOGI(TAG, "    Latitude:    %s°", lat_buf);
    ESP_LOGI(TAG, "    Longitude:   %s°", lon_buf);
    ESP_LOGI(TAG, "    Alt Baro:    %.1f m", uav->location.altitude_baro);
    ESP_LOGI(TAG, "    Alt Geo:     %.1f m", uav->location.altitude_geo);
    ESP_LOGI(TAG, "    Height AGL:  %.1f m (%s)",
             uav->location.height, get_height_ref_name(uav->location.height_ref));
    ESP_LOGI(TAG, "    Direction:   %.1f°", uav->location.direction);
    ESP_LOGI(TAG, "    Speed H:     %s m/s", speed_h_buf);
    ESP_LOGI(TAG, "    Speed V:     %s m/s", speed_v_buf);
    ESP_LOGI(TAG, "    Accuracy:");
    ESP_LOGI(TAG, "      Horiz: %s, Vert: %s, Baro: %s, Speed: %s",
             get_horiz_acc_name(uav->location.h_accuracy),
             get_vert_acc_name(uav->location.v_accuracy),
             get_vert_acc_name(uav->location.baro_accuracy),
             get_speed_acc_name(uav->location.speed_accuracy));
    ESP_LOGI(TAG, "    Timestamp:   %.1f s (accuracy: %.1f s)",
             uav->location.timestamp,
             decodeTimestampAccuracy((ODID_Timestamp_accuracy_t)uav->location.ts_accuracy));
}

static void print_system(const uav_track_t *uav) {
    if (!uav || !uav->system.valid) return;

    char op_lat_buf[20], op_lon_buf[20];

    format_position_buffer(op_lat_buf, sizeof(op_lat_buf), uav->system.operator_latitude, 7);
    format_position_buffer(op_lon_buf, sizeof(op_lon_buf), uav->system.operator_longitude, 7);

    ESP_LOGI(TAG, "  System:");
    ESP_LOGI(TAG, "    Operator Location: %s", get_operator_loc_name(uav->system.operator_location_type));
    ESP_LOGI(TAG, "    Operator Position: %s°, %s°",
             op_lat_buf, op_lon_buf);
    ESP_LOGI(TAG, "    Operator Alt Geo: %.1f m", uav->system.operator_altitude_geo);
    ESP_LOGI(TAG, "    Classification:   %s", get_classification_name(uav->system.classification_type));
    if (uav->system.classification_type == ODID_CLASSIFICATION_TYPE_EU) {
        ESP_LOGI(TAG, "      EU Category: %s", get_eu_category_name(uav->system.category_eu));
        ESP_LOGI(TAG, "      EU Class:    %s", get_eu_class_name(uav->system.class_eu));
    }
    ESP_LOGI(TAG, "    Area Count: %d, Radius: %d m",
             uav->system.area_count, uav->system.area_radius);
    ESP_LOGI(TAG, "    Area Ceiling: %.1f m, Floor: %.1f m",
             uav->system.area_ceiling, uav->system.area_floor);
}

static void print_self_id(const uav_track_t *uav) {
    if (!uav || !uav->self_id.valid) return;
    ESP_LOGI(TAG, "  Self ID: %s - '%s'",
             get_desc_type_name(uav->self_id.description_type), uav->self_id.description);
}

static void print_operator_id(const uav_track_t *uav) {
    if (!uav || !uav->operator_id.valid) return;
    ESP_LOGI(TAG, "  Operator ID: Type=%d, ID='%s'",
             uav->operator_id.id_type, uav->operator_id.id);
}

static void print_auth(const uav_track_t *uav) {
    if (!uav) return;

    for (int i = 0; i < ODID_AUTH_MAX_PAGES; i++) {
        if (!uav->uas_data.AuthValid[i]) continue;
        const ODID_Auth_data *a = &uav->uas_data.Auth[i];
        ESP_LOGI(TAG, "  Auth Page %d: Type=%s", a->DataPage, get_auth_type_name((uint8_t)a->AuthType));
        if (a->DataPage == 0) {
            ESP_LOGI(TAG, "    Last Page: %d, Length: %d bytes", a->LastPageIndex, a->Length);
        }
    }
}

static void print_gb46750(const uav_track_t *uav) {
    if (!uav || !uav->gb46750.valid) return;

    const gb46750_data_t *gb = &uav->gb46750;

    ESP_LOGI(TAG, "  GB 46750 Data:");

    if (gb->has_unique_id) {
        ESP_LOGI(TAG, "    Unique ID:     '%s'", gb->unique_id);
    }
    if (gb->has_realname_flag) {
        ESP_LOGI(TAG, "    Realname ID:    '%s'", gb->realname_id);
    }
    if (gb->has_operation_category) {
        ESP_LOGI(TAG, "    Operation Cat:  %u", gb->operation_category);
    }
    if (gb->has_ua_category) {
        ESP_LOGI(TAG, "    UA Category:    %u", gb->ua_category);
    }
    if (gb->has_rcs_loc_type) {
        ESP_LOGI(TAG, "    RCS Loc Type:   %u", gb->rcs_loc_type);
    }
    if (gb->has_rcs_location) {
        char rcs_lat_buf[20], rcs_lon_buf[20];
        format_position_buffer(rcs_lat_buf, sizeof(rcs_lat_buf), gb->rcs_latitude, 7);
        format_position_buffer(rcs_lon_buf, sizeof(rcs_lon_buf), gb->rcs_longitude, 7);
        ESP_LOGI(TAG, "    RCS Position:   %s°, %s°",
                 rcs_lat_buf, rcs_lon_buf);
    }
    if (gb->has_rcs_altitude) {
        ESP_LOGI(TAG, "    RCS Altitude:   %.1f m", (double)gb->rcs_altitude);
    }
    if (gb->has_uav_location) {
        char uav_lat_buf[20], uav_lon_buf[20];
        format_position_buffer(uav_lat_buf, sizeof(uav_lat_buf), gb->uav_latitude, 7);
        format_position_buffer(uav_lon_buf, sizeof(uav_lon_buf), gb->uav_longitude, 7);
        ESP_LOGI(TAG, "    UAV Position:   %s°, %s°",
                 uav_lat_buf, uav_lon_buf);
    }
    if (gb->has_track_angle) {
        ESP_LOGI(TAG, "    Track Angle:    %.1f°", (double)gb->track_angle);
    }
    if (gb->has_ground_speed) {
        char speed_buf[10];
        format_float_buffer(speed_buf, sizeof(speed_buf), gb->ground_speed, 2);
        ESP_LOGI(TAG, "    Ground Speed:   %s m/s", speed_buf);
    }
    if (gb->has_relative_height) {
        ESP_LOGI(TAG, "    Rel Height:     %.1f m", (double)gb->relative_height);
    }
    if (gb->has_vertical_speed) {
        char speed_buf[10];
        format_float_buffer(speed_buf, sizeof(speed_buf), gb->vertical_speed, 2);
        ESP_LOGI(TAG, "    Vertical Speed: %s m/s", speed_buf);
    }
    if (gb->has_geo_altitude) {
        ESP_LOGI(TAG, "    Geo Altitude:   %.1f m", (double)gb->geo_altitude);
    }
    if (gb->has_baro_altitude) {
        ESP_LOGI(TAG, "    Baro Altitude:  %.1f m", (double)gb->baro_altitude);
    }
    if (gb->has_operation_status) {
        ESP_LOGI(TAG, "    Op Status:      %u", gb->operation_status);
    }
    if (gb->has_coord_system) {
        ESP_LOGI(TAG, "    Coord System:   %u", gb->coord_system);
    }
    if (gb->has_h_accuracy) {
        ESP_LOGI(TAG, "    H Accuracy:     %u", gb->h_accuracy);
    }
    if (gb->has_v_accuracy) {
        ESP_LOGI(TAG, "    V Accuracy:     %u", gb->v_accuracy);
    }
    if (gb->has_speed_accuracy) {
        ESP_LOGI(TAG, "    Speed Accuracy: %u", gb->speed_accuracy);
    }
    if (gb->has_timestamp) {
        ESP_LOGI(TAG, "    Timestamp:      %llu ms", (unsigned long long)gb->timestamp_ms);
    }
    if (gb->has_ts_accuracy) {
        ESP_LOGI(TAG, "    TS Accuracy:    %u", gb->ts_accuracy);
    }
    if (gb->has_ext_byte1 || gb->has_ext_byte2 || gb->has_ext_byte3) {
        ESP_LOGI(TAG, "    Ext Flags:      byte1=%d byte2=%d byte3=%d",
                 gb->has_ext_byte1, gb->has_ext_byte2, gb->has_ext_byte3);
    }
}

void crid_display_uav_detail(const uav_track_t *uav) {
    if (!uav) {
        ESP_LOGW(TAG, "Invalid UAV pointer in detail");
        return;
    }

    char mac_str[18];
    crid_display_mac_str(uav->mac, mac_str, sizeof(mac_str));

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "=== UAV Detail: %s ===", mac_str);
    ESP_LOGI(TAG, "  RSSI: %d dBm, Channel: %d, Messages: %lu",
             uav->last_rssi, uav->last_channel, (unsigned long)uav->msg_count);
    ESP_LOGI(TAG, "  Transport: %s, Protocol: %s",
             get_transport_name(uav->transport), get_protocol_name(uav->protocol));

    /* GB 46750 协议：显示 GB 原始数据，跳过 ASTM 统一视图 */
    if (uav->protocol == RID_PROTOCOL_GB46750 && uav->gb46750.valid) {
        print_basic_id(uav);
        print_gb46750(uav);
    } else {
        print_basic_id(uav);
        print_location(uav);
        print_system(uav);
        print_self_id(uav);
        print_operator_id(uav);
        print_auth(uav);
    }

    ESP_LOGI(TAG, "========================================");
}

/* ================================================================
 * 状态行打印（用于 monitor 列表）
 * ================================================================ */

void crid_display_uav_status(const uav_track_t *uav) {
    if (!uav) {
        ESP_LOGW(TAG, "Invalid UAV pointer in status");
        return;
    }

    char mac_str[18];
    crid_display_mac_str(uav->mac, mac_str, sizeof(mac_str));

    if (!uav->last_seen_ms) {
        ESP_LOGW(TAG, "Invalid timestamp in status");
        return;
    }

    uint32_t age_ms = esp_log_timestamp() - uav->last_seen_ms;
    char age_seconds[10];
    snprintf(age_seconds, sizeof(age_seconds), "%lus", (unsigned long)(age_ms / 1000));

    if (uav->basic_id.valid && uav->location.valid) {
        char lat_buf[20], lon_buf[20], alt_buf[10], spd_buf[10];

        format_position_buffer(lat_buf, sizeof(lat_buf), uav->location.latitude, 5);
        format_position_buffer(lon_buf, sizeof(lon_buf), uav->location.longitude, 5);
        format_float_buffer(alt_buf, sizeof(alt_buf), uav->location.altitude_baro, 0);
        format_float_buffer(spd_buf, sizeof(spd_buf), uav->location.speed_horizontal, 1);

        ESP_LOGI(TAG, "  [%s] %s @ %s,%s (%sm, %s m/s) %s %s ago",
                 mac_str, uav->basic_id.uas_id,
                 lat_buf, lon_buf,
                 alt_buf, spd_buf,
                 get_transport_name(uav->transport),
                 age_seconds);
    } else if (uav->basic_id.valid) {
        ESP_LOGI(TAG, "  [%s] %s (no location) %s %s ago",
                 mac_str, uav->basic_id.uas_id,
                 get_transport_name(uav->transport),
                 age_seconds);
    } else {
        ESP_LOGI(TAG, "  [%s] (no ID) %s %s ago",
                 mac_str, get_transport_name(uav->transport),
                 age_seconds);
    }
}