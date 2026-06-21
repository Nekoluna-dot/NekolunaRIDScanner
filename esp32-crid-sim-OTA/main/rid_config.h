#ifndef CRID_CONFIG_H
#define CRID_CONFIG_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
    // Chinese UAV classification (预留)
enum class CNUAVCategory : uint8_t {
    UNKNOWN = 0,
    MICRO = 1,      // 微型 (< 250g)
    LIGHT = 2,      // 轻型 (250g - 4kg)
    SMALL = 3,      // 小型 (4kg - 25kg)
    MEDIUM = 4,     // 中型 (25kg - 150kg)
    LARGE = 5       // 大型 (> 150kg)
};

// Flight zone type
enum class CNFlightZone : uint8_t {
    UNKNOWN = 0,
    ALLOWED = 1,        // 适飞空域
    RESTRICTED = 2,     // 限制空域
    PROHIBITED = 3      // 禁飞空域
};

#endif



// 飞行模式枚举
typedef enum {
    FLIGHT_MODE_CIRCLE = 0,
    FLIGHT_MODE_PINGPONG = 1,
    FLIGHT_MODE_S_SEARCH = 2,
    FLIGHT_MODE_MAX
} flight_mode_t;

// 动态配置结构体
typedef struct {
    double init_lat;
    double init_lon;
    float speed;
    uint8_t flight_mode;
    uint8_t channel;
} crid_dynamic_config_t;

extern crid_dynamic_config_t g_crid_config;
extern SemaphoreHandle_t g_crid_config_mutex;

#define CRID_NVS_NAMESPACE "crid_cfg"
#define CRID_UAS_ID_MAX_LEN 20
#define CRID_SSID_MAX_LEN 32
#define DEFAULT_WIFI_CHANNEL 6

// 完整配置结构体 (用于报文构建)
typedef struct {
    uint8_t mac_address[6];
    char uas_id[CRID_UAS_ID_MAX_LEN + 1];
    uint8_t id_type;
    uint8_t ua_type;
    float latitude;
    float longitude;
    float altitude_msl;
    float altitude_agl;
    float speed_horizontal;
    float speed_vertical;
    float heading;
    uint8_t status;
    float operator_lat;
    float operator_lon;
    float operator_alt;
    char operator_id[CRID_UAS_ID_MAX_LEN + 1];
    char drone_name[CRID_UAS_ID_MAX_LEN + 1];
    uint8_t operator_location_type;
    uint8_t classification_type;
    uint8_t category_eu;
    uint8_t class_eu;
    uint8_t height_type;
    char ssid[CRID_SSID_MAX_LEN + 1];
    uint8_t channel;
    uint8_t message_counter;
    double base_latitude;
    double base_longitude;
    float base_altitude_msl;
    float patrol_radius_lat;
    float patrol_radius_lon;
    float patrol_speed;
    float time_counter;
} cn_crid_config_t;

// ================= 新增：系统与配置管理 API =================

// 系统信息结构体
typedef struct {
    char chip_model[16];
    char flash_size[8];
    char mac_addr[18];
    uint32_t uptime_sec;
    char sys_time[32];
    uint32_t free_heap;
    char partition_name[16];
} crid_sys_info_t;

// 获取当前系统运行信息
void crid_get_sys_info(crid_sys_info_t *info);

// 线程安全地获取当前配置快照
void crid_get_config_snapshot(crid_dynamic_config_t *cfg);

// 初始化 SNTP 时间同步
void crid_time_sync_init(void);

// NVS 配置管理
esp_err_t crid_nvs_init(void);
esp_err_t crid_nvs_load_config(crid_dynamic_config_t *cfg);
esp_err_t crid_nvs_save_config(const crid_dynamic_config_t *cfg);

// 配置初始化与更新
void crid_config_init_default(cn_crid_config_t *config);
void crid_config_update_position(cn_crid_config_t *config, float lat, float lon,
                                 float alt_msl, float alt_agl, float speed_h, 
                                 float speed_v, float heading);

#ifdef __cplusplus
}
#endif
#endif // CRID_CONFIG_H