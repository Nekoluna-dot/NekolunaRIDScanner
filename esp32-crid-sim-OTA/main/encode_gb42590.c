#include <string.h>
#include <math.h>
#include <time.h>
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/time.h"

#include "sdkconfig.h"
#include "encode_gb42590.h"

static const char *TAG = "ENCODE_GB42590";

// --- 辅助函数：写入 int32_t 为小端序 (安全处理负数，避免有符号右移未定义行为) ---
static inline void write_le32(uint8_t *buf, int32_t val) {
    uint32_t uval = (uint32_t)val; // 强制转为无符号数再进行位移，100% 符合 C 标准
    buf[0] = uval & 0xFF;
    buf[1] = (uval >> 8) & 0xFF;
    buf[2] = (uval >> 16) & 0xFF;
    buf[3] = (uval >> 24) & 0xFF;
}

// --- 辅助函数：写入 uint32_t 为小端序 ---
static inline void write_le32_u32(uint8_t *buf, uint32_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
}

// --- 辅助函数：写入 uint16_t 为小端序 ---
static inline void write_le16(uint8_t *buf, uint16_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
}

// --- 修正：编码高度 (0.5m 精度，偏移 -1000m) ---
// 符合 ASTM F3411 / GB42590 标准公式: Encoded = (Altitude_m + 1000) / 0.5  => 等价于 (Altitude_m + 1000) * 2.0
static uint16_t encode_altitude(float altitude_m) {
    float val = (altitude_m + 1000.0f) * 2.0f;
    
    if (val < 0.0f) {
        val = 0.0f;       // 0 表示无效/未知高度
    }
    if (val > 65535.0f) {
        val = 65535.0f;   // 超出 16-bit 无符号整数最大值 (对应 31767.5m)
    }
    
    // 加上 0.5f 实现四舍五入，避免浮点数截断丢失 0.5m 精度
    return (uint16_t)(val + 0.5f);
}

// --- 编码地速 (符合 ASTM F3411-22a 表) ---
static uint8_t encode_ground_speed(float speed_ms) {
    if (speed_ms < 0.0f) speed_ms = 0.0f;
    if (speed_ms < 63.75f) {
        return (uint8_t)(speed_ms * 4.0f + 0.5f); // 加入 0.5f 四舍五入
    } else if (speed_ms <= 254.25f) {
        return (uint8_t)(255.0f + (speed_ms - 63.75f) / 0.75f + 0.5f);
    } else {
        return 254; // ASTM 标准规定 255 为 Invalid，254 为最大有效值
    }
}

void crid_build_basic_id_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);
    message[0] = (MSG_TYPE_BASIC_ID << 4) | 0x01;
    message[1] = (config->ua_type & 0x0F) | ((config->id_type & 0x0F) << 4);
    
    memset(&message[2], 0x00, CRID_UAS_ID_MAX_LEN);
    size_t id_len = strlen(config->uas_id);
    if (id_len > CRID_UAS_ID_MAX_LEN) id_len = CRID_UAS_ID_MAX_LEN;
    memcpy(&message[2], config->uas_id, id_len);
    
    ESP_LOGD(TAG, "Basic ID message built (UAS: %s)", config->uas_id);
}

void crid_build_location_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);
    message[0] = (MSG_TYPE_LOCATION << 4) | 0x01;
    
    uint8_t speed_mult = (config->speed_horizontal >= 63.75f) ? 1 : 0;
    uint8_t height_type = config->height_type & 0x01;
    message[1] = (config->status << 4) | (height_type << 2) | speed_mult;
    
    uint8_t track_angle;
    if (config->heading < 0 || config->heading >= 360.0f) {
        track_angle = 255;
    } else {
        track_angle = (uint8_t)(config->heading + 0.5f);
        if (track_angle > 254) track_angle = 254;
    }
    message[2] = track_angle;
    message[3] = encode_ground_speed(config->speed_horizontal);
    message[4] = (uint8_t)(config->speed_vertical * 2.0f );
    
    // 【修复】使用 round() 避免浮点截断误差
    write_le32_u32(&message[5], (uint32_t)(int32_t)round(config->latitude * 1e7));
    write_le32_u32(&message[9], (uint32_t)(int32_t)round(config->longitude * 1e7));
    
    write_le16(&message[13], encode_altitude(config->altitude_msl));
    write_le16(&message[15], encode_altitude(config->altitude_msl)); // 几何高度通常与气压高度相近或相同
    write_le16(&message[17], encode_altitude(config->altitude_agl));
    
    message[19] = (0x04 << 4) | 0x0B; // VertAccuracy=4 (<10m), HorizAccuracy=11 (<3m)
    message[20] = 0x04; // SpeedAccuracy=4 (<0.3m/s), BaroAccuracy=0 (Unknown)
    
    struct timeval tv_loc;
    gettimeofday(&tv_loc, NULL);
    struct tm *tm_utc = gmtime(&tv_loc.tv_sec);
    uint16_t ts = (uint16_t)(tm_utc->tm_min * 600 + tm_utc->tm_sec * 10 + tv_loc.tv_usec / 100000);
    write_le16(&message[21], ts);
    
    message[23] = 0x02; // TSAccuracy = 0.2s
    
    ESP_LOGD(TAG, "Location message built (%.6f, %.6f)", config->latitude, config->longitude);
}

void crid_build_system_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);
    message[0] = (MSG_TYPE_SYSTEM << 4) | 0x01;
    
    message[1] = (config->classification_type << 2) | (config->operator_location_type & 0x03);
    
 
    write_le32(&message[2], (int32_t)round(config->operator_lat * 1e7));
    write_le32(&message[6], (int32_t)round(config->operator_lon * 1e7));
    
    write_le16(&message[10], 1);
    message[12] = 0x64; // 100m
    
    write_le16(&message[13], encode_altitude(100.0f));
    write_le16(&message[15], encode_altitude(50.0f));
    
    message[17] = (config->class_eu << 4) | (config->category_eu & 0x0F);
    write_le16(&message[18], encode_altitude(config->operator_alt));
    
    struct timeval tv_sys;
    gettimeofday(&tv_sys, NULL);
    uint32_t ts_since_2019 = (uint32_t)(tv_sys.tv_sec - 1546300800);
    write_le32_u32(&message[20], ts_since_2019);
    
    ESP_LOGD(TAG, "System message built");
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

    REQUIRE_SPACE(24);
    frame[pos++] = 0x80;
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;
    memset(&frame[pos], 0xFF, 6); pos += 6;
    memcpy(&frame[pos], config->mac_address, 6); pos += 6;
    memcpy(&frame[pos], config->mac_address, 6); pos += 6;
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;

    REQUIRE_SPACE(8);
    memset(&frame[pos], 0, 8); pos += 8;
    
    REQUIRE_SPACE(2);
    write_le16(&frame[pos], 100); pos += 2;
    
    REQUIRE_SPACE(2);
    frame[pos++] = 0x21;
    frame[pos++] = 0x04;

    size_t ssid_len = strlen(config->ssid);
    if (ssid_len > UINT8_MAX) {
        ESP_LOGE(TAG, "SSID too long: %u", (unsigned)ssid_len);
        return false;
    }
    REQUIRE_SPACE(2 + ssid_len);
    frame[pos++] = 0x00;
    frame[pos++] = (uint8_t)ssid_len;
    memcpy(&frame[pos], config->ssid, ssid_len); pos += ssid_len;

    REQUIRE_SPACE(2 + 8);
    frame[pos++] = 0x01;
    frame[pos++] = 0x08;
    uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
    memcpy(&frame[pos], rates, 8); pos += 8;

    REQUIRE_SPACE(3);
    frame[pos++] = 0x03;
    frame[pos++] = 0x01;
    frame[pos++] = config->channel;

    #define PACKED_MSG_HEADER_LEN 3
    #define PACKED_MSG_COUNT      3
    #define PACKED_MSG_TOTAL_LEN (PACKED_MSG_HEADER_LEN + PACKED_MSG_COUNT * CRID_MESSAGE_SIZE)
    
    REQUIRE_SPACE(2 + 3 + 1 + 1 + PACKED_MSG_TOTAL_LEN);
    frame[pos++] = 0xDD;
    frame[pos++] = 3 + 1 + 1 + PACKED_MSG_TOTAL_LEN;
    frame[pos++] = CRID_OUI_0;
    frame[pos++] = CRID_OUI_1;
    frame[pos++] = CRID_OUI_2;
    frame[pos++] = CRID_VENDOR_TYPE;
    frame[pos++] = message_counter;

    REQUIRE_SPACE(PACKED_MSG_TOTAL_LEN);
    uint8_t packed_msg[PACKED_MSG_TOTAL_LEN];
    uint8_t packed_pos = 0;
    
    packed_msg[packed_pos++] = 0xF1;
    packed_msg[packed_pos++] = CRID_MESSAGE_SIZE;
    packed_msg[packed_pos++] = PACKED_MSG_COUNT;
    
    uint8_t basic_msg[CRID_MESSAGE_SIZE];
    crid_build_basic_id_message(config, basic_msg);
    memcpy(&packed_msg[packed_pos], basic_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;
    
    uint8_t location_msg[CRID_MESSAGE_SIZE];
    crid_build_location_message(config, location_msg);
    memcpy(&packed_msg[packed_pos], location_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;
    
    uint8_t system_msg[CRID_MESSAGE_SIZE];
    crid_build_system_message(config, system_msg);
    memcpy(&packed_msg[packed_pos], system_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;

    // Note: packed_pos is intentionally not used after this point.
    // The variable tracks the position during message construction above.
    (void)packed_pos; // Suppress unused variable warning if needed
    
    memcpy(&frame[pos], packed_msg, PACKED_MSG_TOTAL_LEN);
    pos += PACKED_MSG_TOTAL_LEN;
    
    *out_len = pos;
    
    // --- 打印 16 进制帧数据 (Hex Dump) 用于调试 ---
    static uint32_t s_frame_build_count = 0;
    s_frame_build_count++;
    
    // 每 10 次打印一次，避免串口日志刷屏
    if ((s_frame_build_count % 100U) == 1U) {
        ESP_LOGI(TAG, "=== Beacon Frame Built: %u bytes, Counter=%u ===", *out_len, message_counter);
        
        char hex_line[55]; // 16字节 * 3字符 + 结束符 = 49，留有余量
        for (uint16_t i = 0; i < *out_len; i += 16) {
            uint16_t len = (*out_len - i) > 16 ? 16 : (*out_len - i);
            int offset = 0;
            
            // 格式化当前行的 16 进制数据
            for (uint16_t j = 0; j < len; j++) {
                offset += snprintf(hex_line + offset, sizeof(hex_line) - offset, "%02X ", frame[i + j]);
            }
            
            // 打印: [偏移量] 16进制数据
            ESP_LOGI(TAG, "[%04X] %s", i, hex_line);
        }
        ESP_LOGI(TAG, "==========================================");
    }
    
    return true;
#undef REQUIRE_SPACE
}