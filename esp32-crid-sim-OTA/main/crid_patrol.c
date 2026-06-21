#include "crid_patrol.h"
#include <math.h>
#include "esp_log.h"
#include "rid_config.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "CN_C-RID_PATROL";
static uint32_t flight_tick = 0;

/**
 * 多模式轨迹状态机计算引擎
 * @param out_lat 计算得到的当前纬度输出
 * @param out_lon 计算得到的当前经度输出
 * @param out_heading 当前航向角输出 (0~360度)
 */
void crid_patrol_calculate_next(double *out_lat, double *out_lon, float *out_heading) {
    flight_tick++;
    double base_lat;
    double base_lon;
    float speed_factor;
    uint8_t flight_mode;

    if (g_crid_config_mutex != NULL) {
        xSemaphoreTake(g_crid_config_mutex, portMAX_DELAY);
    }
    base_lat = g_crid_config.init_lat;
    base_lon = g_crid_config.init_lon;
    speed_factor = g_crid_config.speed;
    flight_mode = g_crid_config.flight_mode;
    if (g_crid_config_mutex != NULL) {
        xSemaphoreGive(g_crid_config_mutex);
    }

    switch (flight_mode) {
        case FLIGHT_MODE_CIRCLE: {
            // 1. 经典圆形巡游模式
            double radius = 0.0005; // 约 50 米半径
            double angle = (flight_tick * speed_factor * 0.1); 
            
            *out_lat = base_lat + radius * sin(angle);
            *out_lon = base_lon + radius * cos(angle);
            // 航向角正切推导
            *out_heading = (float)fmod((angle * 180.0 / M_PI) + 90.0, 360.0);
            break;
        }

        case FLIGHT_MODE_PINGPONG: {
            // 2. 直线往返模式（在东西方向进行拉锯飞行）
            double max_distance = 0.001; // 往返半程最大跨度
            double phase = sin(flight_tick * speed_factor * 0.05);
            
            *out_lat = base_lat; 
            *out_lon = base_lon + (max_distance * phase);
            // 航向角根据极性直接切向 90度（东）或 270度（西）
            *out_heading = (cos(flight_tick * speed_factor * 0.05) >= 0) ? 90.0f : 270.0f;
            break;
        }

        case FLIGHT_MODE_S_SEARCH: {
            // 3. S型搜索模式 ( Lissajous 曲线变种模拟格子扫荡 )
            double scale_x = 0.001;
            double scale_y = 0.0003;
            double t = flight_tick * speed_factor * 0.02;

            *out_lat = base_lat + scale_y * sin(t * 4.0); // 纵向高频摆动
            *out_lon = base_lon + scale_x * sin(t);       // 横向低频主线
            *out_heading = (float)fmod(t * 180.0 / M_PI, 360.0);
            break;
        }

        default:
            *out_lat = base_lat;
            *out_lon = base_lon;
            *out_heading = 0.0f;
            break;
    }
}

void crid_patrol_step(cn_crid_config_t *config) {
    if (config == NULL) return;

    config->time_counter += 1.0f;

    float angle = config->time_counter * config->patrol_speed;

    // 圆形巡游路径
    float new_lat = config->base_latitude +
                    config->patrol_radius_lat * cosf(angle);
    float new_lon = config->base_longitude +
                    config->patrol_radius_lon * sinf(angle);

    // 高度周期性缓慢变化：在 45m ~ 55m 之间波动，周期约 62.8 秒
    // 使用基准高度计算偏移，避免累积误差
    float alt_offset = 5.0f * sinf(config->time_counter * 0.1f);
    float new_alt_msl = config->base_altitude_msl + alt_offset;
    float new_alt_agl = new_alt_msl - 5.0f;

    // 速度变化
    float new_speed_h = 1.5f + 0.5f * sinf(config->time_counter * 0.1f);
    // 垂直速度 = 高度对时间的导数：d/dt[5*sin(0.1*t)] = 0.5*cos(0.1*t)
    // 幅度 ±0.5 m/s，符合低速飞行特征
    float new_speed_v = 0.5f * cosf(config->time_counter * 0.1f);

    // 航向（基于运动切线方向，正北为0°，顺时针增加）
    // 位置: lat = base + r_lat*cos(angle), lon = base + r_lon*sin(angle)
    // d(lat)/dt = -r_lat*sin(angle)*omega, d(lon)/dt = r_lon*cos(angle)*omega
    // 航向 = atan2(dlon, dlat) （注意：atan2(x, y) 给出从y轴顺时针的角度）
    float dlat = -config->patrol_radius_lat * sinf(angle) * config->patrol_speed;
    float dlon = config->patrol_radius_lon * cosf(angle) * config->patrol_speed;
    float new_heading = atan2f(dlon, dlat) * 180.0f / M_PI;
    if (new_heading < 0.0f) new_heading += 360.0f;

    crid_config_update_position(config, new_lat, new_lon,
                                new_alt_msl, new_alt_agl,
                                new_speed_h, new_speed_v,
                                new_heading);

    ESP_LOGI(TAG, "Patrol step: pos=(%.6f,%.6f), alt=%.1fm, hdg=%.1f°, spd=%.1fm/s",
             new_lat, new_lon, new_alt_msl, new_heading, new_speed_h);
}
