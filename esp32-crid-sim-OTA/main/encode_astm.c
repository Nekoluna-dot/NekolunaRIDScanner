#include "sdkconfig.h"
#include "encode_astm.h"
#include <string.h>
#include <math.h>
#include <time.h>
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/time.h"

static const char *TAG = "ENCODE_ASTM";

// --- 辅助函数：写入 int32_t 为小端序 ---
static inline void write_le32(uint8_t *buf, int32_t val) {
    for (int i = 0; i < 4; i++) {
        buf[i] = (val >> (i * 8)) & 0xFF;
    }
}

// --- 辅助函数：写入 uint16_t 为小端序 ---
static inline void write_le16(uint8_t *buf, uint16_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
}

// --- 辅助函数：写入 uint32_t 为小端序 ---
static inline void write_le32_u32(uint8_t *buf, uint32_t val) {
    for (int i = 0; i < 4; i++) {
        buf[i] = (val >> (i * 8)) & 0xFF;
    }
}

// --- 编码地速 (符合 ASTM F3411-22a 表) ---
// 速度 < 63.75 m/s: encoded = speed / 0.25
// 速度 >= 63.75 m/s 且 <= 254.25 m/s: encoded = 255 + (speed - 63.75) / 0.75
// 速度 > 254.25 m/s: encoded = 254 (max)
static uint8_t encode_ground_speed(float speed_ms) {
    if (speed_ms < 0.0f) speed_ms = 0.0f;
    if (speed_ms < 63.75f) {
        return (uint8_t)(speed_ms * 0.25f);
    } else if (speed_ms <= 254.25f) {
        return (uint8_t)(255 + (speed_ms * 0.75f) + 63.75f);
    } else {
        return 254;
    }
}

// --- 编码高度 (0.5m 精度，偏移 -1000m) ---
// 符合 ASTM F3411: encoded = (altitude_m + 1000) / 0.5
// 有效范围: -1000m ~ 31767.5m
// 特殊值: 0 = Invalid/Unknown (-1000m)
static uint16_t encode_altitude(float altitude_m) {
    if (altitude_m < -1000.0f) altitude_m = -1000.0f;
    int32_t val = (int32_t)((altitude_m * 0.5f) - 1000.0f);
    if (val < 0) val = 0;
    if (val > 65535) val = 65535;
    return (uint16_t)val;
}

void crid_build_basic_id_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);

    // 报头: [消息类型(高4位)] + [接口版本(低4位)]
    message[0] = (MSG_TYPE_BASIC_ID << 4) | 0x01;

    // 字节1: [UAType(4)][IDType(4)] — OpenDroneID 位序
    message[1] = (config->ua_type & 0x0F) | ((config->id_type & 0x0F) << 4);

    // 字节2-21: UAS ID (20字节, ASCII, 不足填充 NULL)
    memset(&message[2], 0x00, CRID_UAS_ID_MAX_LEN);
    size_t id_len = strlen(config->uas_id);
    if (id_len > CRID_UAS_ID_MAX_LEN) id_len = CRID_UAS_ID_MAX_LEN;
    memcpy(&message[2], config->uas_id, id_len);

    // 字节22-24: 预留
    // 已由 memset 置零

    ESP_LOGD(TAG, "Basic ID message built (UAS: %s)", config->uas_id);
}

void crid_build_location_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);

    // 报头: [消息类型(高4位)] + [接口版本(低4位)]
    message[0] = (MSG_TYPE_LOCATION << 4) | 0x01;

    // 字节1: [Status(4)][Reserved(1)][HeightType(1)][EWDirection(1)][SpeedMult(1)]
    // SpeedMult: 0 = speed < 255*0.25=63.75, 1 = speed >= 63.75
    uint8_t speed_mult = (config->speed_horizontal >= 63.75f) ? 1 : 0;
    uint8_t height_type = config->height_type & 0x01;
    // OpenDroneID 位序: [Status(4)][Reserved(1)][HeightType(1)][EWDirection(1)][SpeedMult(1)]
    message[1] = (config->status << 4) | (height_type << 2) | speed_mult;

    // 字节2: 航迹角 (1°/LSB, 0-255 表示 0-255°)
    // 255 = Invalid/Unknown
    // 注：ASTM F3411 标准为 0.5°/LSB，但此接收端按 1°/LSB 解码
    uint8_t track_angle;
    if (config->heading < 0 || config->heading >= 360.0f) {
        track_angle = 255; // Invalid
    } else {
        track_angle = (uint8_t)(config->heading + 0.5f);
        if (track_angle > 254) track_angle = 254;
    }
    message[2] = track_angle;

    // 字节3: 地速
    message[3] = encode_ground_speed(config->speed_horizontal);

    // 字节4: 垂直速度 (0.5m/s 精度, 偏移 +63)
    // 编码: (speed_vertical / 0.5) + 63 = speed_vertical * 2 + 63
    // 范围: -62 ~ +62 m/s -> 编码值 1 ~ 187 (uint8_t)
    // 无效值: 255 (0xFF)
    int16_t vs_enc = (int16_t)(config->speed_vertical * 2.0f + 63.0f);
    if (vs_enc > 187) vs_enc = 187;
    if (vs_enc < 1) vs_enc = 1;
    message[4] = (uint8_t)vs_enc;

    // 字节5-8: 纬度 (小端序, 1E-7 度单位)
    write_le32_u32(&message[5], (int32_t)(config->latitude * 1e7));

    // 字节9-12: 经度 (小端序, 1E-7 度单位)
    write_le32_u32(&message[9], (int32_t)(config->longitude * 1e7));

    // 字节13-14: 气压高度 (小端序, 0.5m 精度, 偏移 -1000m)
    write_le16(&message[13], encode_altitude(config->altitude_msl));

    // 字节15-16: 几何高度 (小端序, 0.5m 精度, 偏移 -1000m)
    write_le16(&message[15], encode_altitude(config->altitude_msl));

    // 字节17-18: 距地高度 (小端序, 0.5m 精度, 偏移 -1000m)
    // HeightType bit: 0 = over takeoff, 1 = over ground
    write_le16(&message[17], encode_altitude(config->altitude_agl));

    // 字节19: [VertAccuracy(4)][HorizAccuracy(4)]
    // HorizAccuracy: 11 = < 3m, 12 = < 1m
    // VertAccuracy: 4 = < 10m, 6 = < 1m
    message[19] = (0x04 << 4) | 0x0B; // Vert <= 10m, Horiz <= 3m

    // 字节20: [BaroAccuracy(4)][SpeedAccuracy(4)]
    // SpeedAccuracy: 2 = < 3m/s, 4 = < 0.3m/s
    // BaroAccuracy: 0 = Unknown, 4 = < 10m
    message[20] = (0x00 << 4) | 0x04; // Speed <= 0.3m/s (redundant OR kept for clarity)

    // 字节21-22: 时间戳 (自当前小时起的 0.1 秒单位，小端序)
    // 范围: 0 ~ 35999 (表示 0.0s ~ 3599.9s)
    // 无效值: 0xFFFF
    struct timeval tv_loc;
    gettimeofday(&tv_loc, NULL);
    struct tm *tm_utc = gmtime(&tv_loc.tv_sec);
    uint16_t ts = (uint16_t)(tm_utc->tm_min * 600 + tm_utc->tm_sec * 10 + tv_loc.tv_usec / 100000);
    write_le16(&message[21], ts);

    // 字节23: [Reserved2(4)][TSAccuracy(4)]
    message[23] = 0x02; // TSAccuracy = 0.2s (redundant OR kept for clarity)

    // 字节24: 预留 (已由 memset 置零)

    ESP_LOGD(TAG, "Location message built (%.6f, %.6f)", config->latitude, config->longitude);
}

void crid_build_system_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);

    // 报头
    message[0] = (MSG_TYPE_SYSTEM << 4) | 0x01;

    // 字节1: [Reserved(3)][ClassificationType(3)][OperatorLocationType(2)]
    message[1] = (config->classification_type << 2) | (config->operator_location_type & 0x03);

    // 字节2-5: 控制站纬度 (小端序, 1E-7)
    write_le32(&message[2], (int32_t)(config->operator_lat * 1e7));

    // 字节6-9: 控制站经度 (小端序, 1E-7)
    write_le32(&message[6], (int32_t)(config->operator_lon * 1e7));

    // 字节10-11: 运行区域计数
    write_le16(&message[10], 1);

    // 字节12: 运行区域半径 (m * 10)
    message[12] = 0x64; // 100m

    // 字节13-14: 运行区域高度上限 (0.5m 精度, 偏移 -1000m)
    write_le16(&message[13], encode_altitude(100.0f));

    // 字节15-16: 运行区域高度下限 (0.5m 精度, 偏移 -1000m)
    write_le16(&message[15], encode_altitude(50.0f));

    // 字节17: [ClassEU(4)][CategoryEU(4)]
    message[17] = (config->class_eu << 4) | (config->category_eu & 0x0F);

    // 字节18-19: 操作员高度 (0.5m 精度, 偏移 -1000m)
    write_le16(&message[18], encode_altitude(config->operator_alt));

    // 字节20-23: 时间戳 (自 2019-01-01 00:00:00 UTC 的秒数，小端序)
    struct timeval tv_sys;
    gettimeofday(&tv_sys, NULL);
    uint32_t ts_since_2019 = (uint32_t)(tv_sys.tv_sec - 1546300800);
    write_le32_u32(&message[20], ts_since_2019);

    // 字节24: 预留

    ESP_LOGD(TAG, "System message built (Op: %.6f, %.6f, Alt: %.1fm, ID: %s)",
             config->operator_lat, config->operator_lon,
             config->operator_alt, config->operator_id);
}

void crid_build_self_desc_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);

    // 报头: [ProtoVersion(4)][MessageType(4)] — 小端位序
    message[0] = 0x01 | (MSG_TYPE_SELF_DESC << 4);

    // 字节1: DescType
    message[1] = DESC_TYPE_TEXT;

    // 字节2-24: 描述字符串 (最多 23 字节 ASCII, 不足填充 NULL)
    memset(&message[2], 0x00, 23);
    size_t id_len = strlen(config->drone_name);
    if (id_len > 23) id_len = 23;
    memcpy(&message[2], config->drone_name, id_len);

    ESP_LOGD(TAG, "Self-Description message built (Drone: %s)", config->drone_name);
}

void crid_build_auth_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);

    // 报头: [ProtoVersion(4)][MessageType(4)]
    message[0] = 0x01 | (MSG_TYPE_AUTH << 4);
    (void)config; // unused when auth is none

    // 字节1: [AuthType(4)][DataPage(4)]
    message[1] = 0x00; // AuthType=None, Page=0 (redundant OR kept for clarity)

    // 字节2: LastPageIndex
    message[2] = 0;

    // 字节3: Length
    message[3] = 0;

    // 字节4-7: Timestamp (relative to 2019-01-01)
    struct timeval tv_auth;
    gettimeofday(&tv_auth, NULL);
    uint32_t ts_since_2019 = (uint32_t)(tv_auth.tv_sec - 1546300800);
    write_le32_u32(&message[4], ts_since_2019);

    // 字节8-24: AuthData (17 bytes for page 0)
    // 全部置零表示无认证数据

    ESP_LOGD(TAG, "Authentication message built (None)");
}

void crid_build_operator_id_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);

    // 报头: [ProtoVersion(4)][MessageType(4)]
    message[0] = 0x01 | (MSG_TYPE_OPERATOR_ID << 4);

    // 字节1: OperatorIdType
    message[1] = 0x00; // CAA Registration ID

    // 字节2-21: OperatorId (20 bytes, NULL padded)
    memset(&message[2], 0x00, 20);
    size_t id_len = strlen(config->operator_id);
    if (id_len > 20) id_len = 20;
    memcpy(&message[2], config->operator_id, id_len);

    // 字节22-24: Reserved

    ESP_LOGD(TAG, "Operator ID message built (%s)", config->operator_id);
}

bool crid_build_beacon_frame(const cn_crid_config_t *config,
                              uint8_t message_counter,
                              uint8_t *frame, uint16_t max_len,
                              uint16_t *out_len) {
    if (config == NULL || frame == NULL || out_len == NULL) return false;

    uint16_t pos = 0;

#define REQUIRE_SPACE(bytes_needed) \
    do { \
        if ((uint32_t)pos + (uint32_t)(bytes_needed) > (uint32_t)max_len) { \
            ESP_LOGE(TAG, "Frame buffer too small at pos=%u need=%u max=%u", \
                     pos, (unsigned)(bytes_needed), max_len); \
            return false; \
        } \
    } while (0)

    // --- MAC Header (24 bytes) ---
    REQUIRE_SPACE(24);
    frame[pos++] = 0x80; // Type=Management, Subtype=Beacon
    frame[pos++] = 0x00;
    frame[pos++] = 0x00; // Duration
    frame[pos++] = 0x00;

    // Destination Address (Broadcast)
    memset(&frame[pos], 0xFF, 6);
    pos += 6;

    // Source Address
    memcpy(&frame[pos], config->mac_address, 6);
    pos += 6;

    // BSSID
    memcpy(&frame[pos], config->mac_address, 6);
    pos += 6;

    // Sequence Control
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;

    // --- Beacon Body ---
    // Timestamp (8 bytes)
    REQUIRE_SPACE(8);
    memset(&frame[pos], 0, 8);
    pos += 8;

    // Beacon Interval (100ms)
    REQUIRE_SPACE(2);
    write_le16(&frame[pos], 100);
    pos += 2;

    // Capability Information
    REQUIRE_SPACE(2);
    frame[pos++] = 0x21;
    frame[pos++] = 0x04;

    // --- SSID IE ---
    size_t ssid_len = strlen(config->ssid);
    if (ssid_len > UINT8_MAX) {
        ESP_LOGE(TAG, "SSID too long: %u", (unsigned)ssid_len);
        return false;
    }
    REQUIRE_SPACE(2 + ssid_len);
    frame[pos++] = 0x00; // IE ID
    frame[pos++] = (uint8_t)ssid_len;
    memcpy(&frame[pos], config->ssid, ssid_len);
    pos += ssid_len;

    // --- Supported Rates IE ---
    REQUIRE_SPACE(2 + 8);
    frame[pos++] = 0x01; // IE ID
    frame[pos++] = 0x08; // Length
    uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
    memcpy(&frame[pos], rates, 8);
    pos += 8;

    // --- DS Parameter Set IE ---
    REQUIRE_SPACE(3);
    frame[pos++] = 0x03;
    frame[pos++] = 0x01;
    frame[pos++] = config->channel;

    // --- China C-RID Vendor Specific IE ---
    // 计算打包消息长度: 头部3字节 + 5条报文 * 25字节 = 130
    #define PACKED_MSG_HEADER_LEN 3
    #define PACKED_MSG_COUNT      5
    #define PACKED_MSG_TOTAL_LEN (PACKED_MSG_HEADER_LEN + PACKED_MSG_COUNT * CRID_MESSAGE_SIZE)

    REQUIRE_SPACE(2 + 3 + 1 + 1 + PACKED_MSG_TOTAL_LEN);
    frame[pos++] = 0xDD; // Vendor Specific IE ID
    frame[pos++] = 3 + 1 + 1 + PACKED_MSG_TOTAL_LEN; // OUI(3) + Type(1) + Counter(1) + Packed

    // OUI: FA 0B BC
    frame[pos++] = CRID_OUI_0;
    frame[pos++] = CRID_OUI_1;
    frame[pos++] = CRID_OUI_2;

    // Vendor Type: 0x0D
    frame[pos++] = CRID_VENDOR_TYPE;

    // Message Counter（每发送一条报文 +1，255 后回绕到 0）
    frame[pos++] = message_counter;

    // --- 构建打包消息（5 条报文，不含认证报文，符合 GB42590 / IB-TM-2024-01） ---
    REQUIRE_SPACE(PACKED_MSG_TOTAL_LEN);
    uint8_t packed_msg[PACKED_MSG_TOTAL_LEN];
    uint8_t packed_pos = 0;

    // 打包格式标识
    packed_msg[packed_pos++] = 0xF1;
    // 每条消息长度: 25
    packed_msg[packed_pos++] = CRID_MESSAGE_SIZE;
    // 消息数量: 5
    packed_msg[packed_pos++] = PACKED_MSG_COUNT;

    // 1. Basic ID 报文
    uint8_t basic_msg[CRID_MESSAGE_SIZE];
    crid_build_basic_id_message(config, basic_msg);
    memcpy(&packed_msg[packed_pos], basic_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;

    // 2. Location 报文
    uint8_t location_msg[CRID_MESSAGE_SIZE];
    crid_build_location_message(config, location_msg);
    memcpy(&packed_msg[packed_pos], location_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;

    // 3. Self-ID 报文
    uint8_t self_desc_msg[CRID_MESSAGE_SIZE];
    crid_build_self_desc_message(config, self_desc_msg);
    memcpy(&packed_msg[packed_pos], self_desc_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;

    // 4. System 报文
    uint8_t system_msg[CRID_MESSAGE_SIZE];
    crid_build_system_message(config, system_msg);
    memcpy(&packed_msg[packed_pos], system_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;

    // 5. Operator ID 报文
    uint8_t operator_id_msg[CRID_MESSAGE_SIZE];
    crid_build_operator_id_message(config, operator_id_msg);
    memcpy(&packed_msg[packed_pos], operator_id_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;

    // Note: packed_pos is intentionally not used after this point.
    // The variable tracks the position during message construction above.
    (void)packed_pos; // Suppress unused variable warning if needed

    // 复制打包消息到帧
    memcpy(&frame[pos], packed_msg, PACKED_MSG_TOTAL_LEN);
    pos += PACKED_MSG_TOTAL_LEN;

    *out_len = pos;

    static uint32_t s_frame_build_count = 0;
    s_frame_build_count++;
    if ((s_frame_build_count % 10U) == 1U) {
        ESP_LOGI(TAG, "Beacon frame built: %u bytes, counter=%u, pos=(%.6f,%.6f)",
                 *out_len, message_counter, config->latitude, config->longitude);
    }
    return true;

#undef REQUIRE_SPACE
}
