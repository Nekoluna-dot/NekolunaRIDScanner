/**
 * crid_parser_gb46750.c — GB 46750-2025 协议解析模块
 *
 * 专门处理 GB 46750-2025 协议的数据解析
 */

#include <string.h>
#include "esp_log.h"
#include "opendroneid.h"
#include "odid_wifi.h"
#include "crid_parser.h"
#include "crid_json.h"
#include "crid_rx_types.h"

static const char *TAG = "RID_GB46750";

/* ================================================================
 * 常量与宏定义
 * ================================================================ */
#define GB46750_MAGIC           0xFF
#define GB46750_VER_MAJOR_MASK  0x07
#define GB46750_VER_MINOR_MASK  0x1F
#define GB46750_VER_MAJOR_SHIFT 5
#define GB46750_VALID_MAJOR     0x01
#define GB46750_HEADER_LEN      6   /* Magic(1)+Ver(1)+Len(1)+Flags(3) */

/* 物理量合理性校验宏 (防止错位读取产生荒谬值) */
#define IS_VALID_LAT(lat)       ((lat) >= -90.0f && (lat) <= 90.0f)
#define IS_VALID_LON(lon)       ((lon) >= -180.0f && (lon) <= 180.0f)
#define IS_VALID_SPEED(speed)   ((speed) >= 0.0f && (speed) <= 300.0f)
#define IS_VALID_ANGLE(angle)   ((angle) >= 0.0f && (angle) < 360.0f)
#define IS_VALID_HEIGHT(h)      ((h) >= -1000.0f && (h) <= 10000.0f)

/* ================================================================
 * 内部辅助函数
 * ================================================================ */
static inline uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline int32_t le32s(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

/**
 * 高度解码：编码值 = (实际值 + 1000) × 2，分辨率 0.5m
 * 编码值 == 0 或 0xFFFF 表示未知
 */
static inline bool decode_alt_2byte(const uint8_t *p, float *out) {
    uint16_t raw = le16(p);

    // 增加对 0xFFFF 的校验（国标中 0xFFFF 通常也代表无效/未知）
    if (raw == 0 || raw == 0xFFFF) {
        *out = 0.0f;
        return false;
    }

    *out = (raw / 2.0f) - 1000.0f;

    // 优化日志：直接打印完整的 raw 十六进制，以及拆解的字节序，方便比对
    //ESP_LOGI(TAG, "Height: raw=0x%04X (bytes: %02X %02X) => alt=%.2f m",              raw, p[0], p[1], *out);

    return true;
}

/**
 * 经纬度解码：8 字节小端序，int32 × 1e-7 度
 * 返回 true 表示经纬度在合理范围内
 */
static inline bool decode_lon_lat(const uint8_t *p, float *lon, float *lat) {
    *lon = le32s(&p[0]) / 1e7;
    *lat = le32s(&p[4]) / 1e7;
    return IS_VALID_LAT(*lat) && IS_VALID_LON(*lon);
}

/* ================================================================
 * GB 46750-2025 数据内容解析
 * ================================================================ */
static int decode_gb46750_payload(gb46750_data_t *gb,
                                  const uint8_t *flags, uint8_t num_flags,
                                  const uint8_t *content, uint8_t content_len) {

    int offset = 0;
    int items_parsed = 0;

    for (uint8_t byte_idx = 0; byte_idx < num_flags && byte_idx < 3; byte_idx++) {
        uint8_t flag = flags[byte_idx];
        // 按位从高到低 (0x80→0x02) 解析，bit 0 (0x01) 为扩展标志位
        for (int8_t bit = 7; bit >= 1; bit--) {
            if (!(flag & (1U << bit))) continue;

            uint8_t item_id = (byte_idx << 3) | bit;
            // ESP_LOGI(TAG, "item_id %02d, offset: %d", item_id, offset);
            // hex_dump(TAG, "Content_RAW", &content[offset],  8);
            switch (item_id) {
                /* 标识字节 1 */
                case 0x07:
                    if (offset + 20 > content_len) return items_parsed;
                    memcpy(gb->unique_id, &content[offset], 20);
                    offset += 20;
                    gb->unique_id[20] = '\0';
                    gb->has_unique_id = true;
                    break;
                case 0x06:
                    if (offset + 8 > content_len) return items_parsed;
                    memcpy(gb->realname_id, &content[offset], 8);
                    offset += 8;
                    gb->realname_id[8] = '\0';
                    gb->has_realname_flag = true;
                    break;
                case 0x05:
                    if (offset + 1 > content_len) return items_parsed;
                    gb->operation_category = content[offset];
                    offset += 1;
                    gb->has_operation_category = true;
                    break;
                case 0x04:
                    if (offset + 1 > content_len) return items_parsed;
                    gb->ua_category = content[offset];
                    offset += 1;
                    gb->has_ua_category = true;
                    break;
                case 0x03:
                    if (offset + 1 > content_len) return items_parsed;
                    gb->rcs_loc_type = content[offset];
                    offset += 1;
                    gb->has_rcs_loc_type = true;
                    break;
                case 0x02:
                    if (offset + 8 > content_len) return items_parsed;
                    {
                        // 使用独立的局部变量，避免与其他 case 的 lon/lat 冲突
                        float rcs_lon = le32s(&content[offset]) / 1e7;
                        float rcs_lat = le32s(&content[offset + 4]) / 1e7;
                        if (IS_VALID_LAT(rcs_lat) && IS_VALID_LON(rcs_lon)) {
                            gb->rcs_longitude = rcs_lon;
                            gb->rcs_latitude  = rcs_lat;
                            gb->has_rcs_location = true;
                        }
                    }
                    offset += 8;
                    break;
                case 0x01:
                    if (offset + 2 > content_len) return items_parsed;
                    {
                        uint16_t raw = le16(&content[offset]);
                        if (raw == 0 || raw == 0xFFFF) {
                            gb->rcs_altitude = 0.0f;
                            gb->has_rcs_altitude = false;
                        } else {
                            gb->rcs_altitude = (raw / 2.0f) - 1000.0f;
                            gb->has_rcs_altitude = true;
                        }
                        // ESP_LOGI(TAG, "rcs_altitude: raw=0x%04X => alt=%.2f m", raw, gb->rcs_altitude);
                    }
                    offset += 2;
                    break;

                /* 标识字节 2 */
                case 0x0F:
                    if (offset + 8 > content_len) return items_parsed;
                    {
                        // 使用独立的局部变量
                        float uav_lon = le32s(&content[offset]) / 1e7;
                        float uav_lat = le32s(&content[offset + 4]) / 1e7;
                        if (IS_VALID_LAT(uav_lat) && IS_VALID_LON(uav_lon)) {
                            gb->uav_longitude = uav_lon;
                            gb->uav_latitude  = uav_lat;
                            gb->has_uav_location = true;
                        }
                    }
                    offset += 8;
                    break;
                case 0x0E:
                    if (offset + 2 > content_len) return items_parsed;
                    {
                        uint16_t raw = le16(&content[offset]);
                        if (raw != 0xFFFF) {
                            float angle = raw / 10.0f;
                            if (IS_VALID_ANGLE(angle)) {
                                gb->track_angle = angle;
                                gb->has_track_angle = true;
                            }
                        }
                    }
                    offset += 2;
                    break;
                case 0x0D:
                    if (offset + 2 > content_len) return items_parsed;
                    {
                        uint16_t raw = le16(&content[offset]);
                        if (raw != 0xFFFF) {
                            float speed = raw / 10.0f;
                            if (IS_VALID_SPEED(speed)) {
                                gb->ground_speed = speed;
                                gb->has_ground_speed = true;
                            }
                        }
                    }
                    offset += 2;
                    break;
                case 0x0C:
                    if (offset + 2 > content_len) return items_parsed;
                    {
                        uint16_t raw = le16(&content[offset]);
                        if (raw != 0) {
                            float height = (raw / 2.0f) - 9000.0f;
                            if (IS_VALID_HEIGHT(height)) {
                                gb->relative_height = height;
                                gb->has_relative_height = true;
                            }
                        }
                    }
                    offset += 2;
                    break;
                case 0x0B:
                    if (offset + 1 > content_len) return items_parsed;
                    {
                        uint8_t raw = content[offset];
                        if (raw != 0xFF) {
                            float v = (raw & 0x7F) / 2.0f;
                            float vs = (raw & 0x80) ? -v : v;
                            float abs_vs = (vs < 0.0f) ? -vs : vs;
                            if (IS_VALID_SPEED(abs_vs)) {
                                gb->vertical_speed = vs;
                                gb->has_vertical_speed = true;
                            }
                        }
                    }
                    offset += 1;
                    break;
                case 0x0A:
                    if (offset + 2 > content_len) return items_parsed;
                    gb->has_geo_altitude = decode_alt_2byte(&content[offset], &gb->geo_altitude);
                    offset += 2;
                    break;
                case 0x09:
                    if (offset + 2 > content_len) return items_parsed;
                    gb->has_baro_altitude = decode_alt_2byte(&content[offset], &gb->baro_altitude);
                    offset += 2;
                    break;

                /* 标识字节 3 */
                case 0x17:
                    if (offset + 1 > content_len) return items_parsed;
                    gb->operation_status = content[offset];
                    offset += 1;
                    gb->has_operation_status = true;
                    break;
                case 0x16:
                    if (offset + 1 > content_len) return items_parsed;
                    gb->coord_system = content[offset];
                    offset += 1;
                    gb->has_coord_system = true;
                    break;
                case 0x15:
                    if (offset + 1 > content_len) return items_parsed;
                    gb->h_accuracy = content[offset];
                    offset += 1;
                    gb->has_h_accuracy = true;
                    break;
                case 0x14:
                    if (offset + 1 > content_len) return items_parsed;
                    gb->v_accuracy = content[offset];
                    offset += 1;
                    gb->has_v_accuracy = true;
                    break;
                case 0x13:
                    if (offset + 1 > content_len) return items_parsed;
                    gb->speed_accuracy = content[offset];
                    offset += 1;
                    gb->has_speed_accuracy = true;
                    break;
                case 0x12:
                    if (offset + 6 > content_len) return items_parsed;
                    {
                        uint64_t ts = 0;
                        for (int i = 0; i < 6; i++)
                            ts |= ((uint64_t)content[offset + i]) << (i * 8);
                        gb->timestamp_ms = ts;
                        gb->has_timestamp = true;
                    }
                    offset += 6;
                    break;
                case 0x11:
                    if (offset + 1 > content_len) return items_parsed;
                    gb->ts_accuracy = content[offset];
                    offset += 1;
                    gb->has_ts_accuracy = true;
                    break;

                default: break;
            }
            items_parsed++;
        }
        // 记录扩展标志位 (bit 0)
        if (flag & 0x01) {
            if (byte_idx == 0) gb->has_ext_byte1 = true;
            else if (byte_idx == 1) gb->has_ext_byte2 = true;
            else if (byte_idx == 2) gb->has_ext_byte3 = true;
        }
    }

    if (items_parsed > 0) {
        gb->valid = true;
    }
    return items_parsed;
}

/**
 * 解析 GB 46750 协议数据
 */
bool crid_parser_decode_gb46750(uav_track_t *uav, const uint8_t *data, uint8_t len) {
    if (!data || len < 1) return false;

    /* 策略 1: GB 46750-2025 (无 Counter 字节) */
    if (len >= GB46750_HEADER_LEN && data[0] == GB46750_MAGIC) {
        uint8_t version     = data[1];
        uint8_t major_ver   = (version >> GB46750_VER_MAJOR_SHIFT) & GB46750_VER_MAJOR_MASK;
        uint8_t content_len = data[2];
        const uint8_t *flags = &data[3];
        const uint8_t *content = &data[6];

        if (major_ver == GB46750_VALID_MAJOR && content_len <= (len - GB46750_HEADER_LEN)) {
            int items = decode_gb46750_payload(&uav->gb46750, flags, 3, content, content_len);

            // [关键修复] 健康检查：防止将非 GB46750 数据误判为此协议
            if (uav->gb46750.has_unique_id) {
                bool has_printable = false;
                for (int i = 0; i < 20; i++) {
                    if (uav->gb46750.unique_id[i] >= 32 && uav->gb46750.unique_id[i] <= 126) {
                        has_printable = true;
                        break;
                    }
                }
                if (!has_printable) {
                    // 极可能是误判 (如 ASTM 包碰巧满足条件)，重置并继续尝试其他协议
                    memset(&uav->gb46750, 0, sizeof(gb46750_data_t));
                    items = 0;
                }
            }

            if (items > 0) {
                return true;
            }
        }
    }
    return false;
}