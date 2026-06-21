/**
 * @file crid-sim.c
 * @brief ESP32 中国民用无人机远程识别 (C-RID) 模拟发射器 - 主入口 (重构优化版)
 *
 * 符合 GB42590-2023 和《民用微轻小型无人驾驶航空器运行识别最低性能要求（试行）》
 */
#include "esp_system.h"
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "rid_config.h"
#include "encode_gb42590.h"
#include "rid_wifi.h"
#include "crid_patrol.h"

static const char *TAG = "RID_MAIN";

static const char *s_reset_reason_str(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_UNKNOWN:    return "未知";
        case ESP_RST_POWERON:   return "上电复位";
        case ESP_RST_EXT:       return "外部引脚复位";
        case ESP_RST_SW:        return "软件复位(esp_restart)";
        case ESP_RST_PANIC:     return "异常崩溃复位";
        case ESP_RST_INT_WDT:    return "中断看门狗复位";
        case ESP_RST_TASK_WDT:  return "任务看门狗复位";
        case ESP_RST_WDT:       return "其他看门狗复位";
        case ESP_RST_DEEPSLEEP: return "深度睡眠唤醒";
        case ESP_RST_BROWNOUT:  return "掉电复位(brownout)";
        case ESP_RST_SDIO:      return "SDIO复位";
        case ESP_RST_USB:       return "USB复位";
        case ESP_RST_JTAG:      return "JTAG复位";
        case ESP_RST_EFUSE:     return "Efuse错误复位";
        case ESP_RST_PWR_GLITCH:return "电源毛刺复位";
        case ESP_RST_CPU_LOCKUP:return "CPU死锁复位";
        default:                return "未知";
    }
}

// --- 全局 Beacon 帧缓冲区（仅在发送任务中使用） ---
#define BEACON_FRAME_BUF_SIZE 512
static uint8_t g_beacon_frame[BEACON_FRAME_BUF_SIZE];
static uint16_t g_beacon_frame_len = 0;
static cn_crid_config_t g_beacon_config;

/**
 * @brief C-RID Beacon 发送任务
 * 每秒(1Hz)动态更新坐标，并构筑标准的 C-RID 帧向空中广播
 */
static void crid_send_beacon_task(void *pvParameter) {
    ESP_LOGI(TAG, "Starting China C-RID beacon transmission loop (1Hz)...");

    TickType_t xLastWakeTime = xTaskGetTickCount();
    // 严格 1Hz 广播周期（通常国内规定为 1000ms 间隔）
    const TickType_t xInterval = pdMS_TO_TICKS(1000); 
    static uint32_t s_tx_count = 0;

    for (;;) {
        // --- 严格 1 秒定时锁定 ---
        vTaskDelayUntil(&xLastWakeTime, xInterval);

        // 每次TX前诊断输出
        ESP_LOGI(TAG, "TX#%lu | 复位:%s | heap:%lu",
                 (unsigned long)s_tx_count, s_reset_reason_str(esp_reset_reason()),
                 (unsigned long)esp_get_free_heap_size());

        // 1. 核心改进：在每次发射前，调用多模式轨迹状态机，计算下一步的动态位置
        // 算法会直接读取由 CLI/NVS 动态改写的全局变量 g_crid_config 中的 init_lat/lon 和 flight_mode
        double current_lat = 0;
        double current_lon = 0;
        float current_heading = 0;
        crid_patrol_calculate_next(&current_lat, &current_lon, &current_heading);
        s_tx_count++;

        // 2. 将计算出来的动态经纬度、航向同步更新到实际要打包的报文数据结构中
        crid_config_update_position(&g_beacon_config,
                        (float)current_lat,
                        (float)current_lon,
                        g_beacon_config.altitude_msl,
                        g_beacon_config.altitude_agl,
                        g_beacon_config.speed_horizontal,
                        g_beacon_config.speed_vertical,
                        current_heading);

        if ((s_tx_count % 50U) == 1U) {
            ESP_LOGI(TAG, "[📡 TX POOL] Mode:%d | Lat: %.6f, Lon: %.6f | Heading: %.1f°", 
                     g_crid_config.flight_mode, current_lat, current_lon, current_heading);
        }

        // 3. 实时重新构建 3-5 条国标消息组合成的完整 Beacon 原始数据帧
        uint8_t message_counter = g_beacon_config.message_counter;
        if (crid_build_beacon_frame(&g_beacon_config, message_counter,
                                    g_beacon_frame, BEACON_FRAME_BUF_SIZE, &g_beacon_frame_len)) {
            // 4. 底层射频注入空中
            esp_err_t ret = crid_wifi_send_raw_frame(g_beacon_frame, g_beacon_frame_len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Raw frame injection failed: %s", esp_err_to_name(ret));
            } else {
                g_beacon_config.message_counter++;
            }
        } else {
            ESP_LOGE(TAG, "Failed to build dynamically updated beacon frame!");
        }
    }
}

/**
 * @brief 应用主入口
 */
void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32 China C-RID Transmitter ===");
    ESP_LOGI(TAG, "Standard: GB42590-2023 Deploying...");
    printf("reset reason: %d\n", esp_reset_reason());

    // 诊断: 读取复位原因
    esp_reset_reason_t rst_reason = esp_reset_reason();
    ESP_LOGI(TAG, "复位原因: %s", s_reset_reason_str(rst_reason));

    // 延迟让 USB 电源稳定后再初始化 Wi-Fi 射频
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 1. 初始化持久化 Flash (NVS)
    esp_err_t ret = crid_nvs_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS Flash Init Failed!");
        return;
    }

    // 2. 运行时加载用户历史保存的动态配置 (若第一次启动则加载越秀山初始默认值)
    // 配置将直接注入到全局变量 g_crid_config 中
    ESP_ERROR_CHECK(crid_nvs_load_config(&g_crid_config));

    // 2.1 初始化用于报文构建的完整配置，并从动态配置同步巡航参数
    crid_config_init_default(&g_beacon_config);
    g_beacon_config.base_latitude = (float)g_crid_config.init_lat;
    g_beacon_config.base_longitude = (float)g_crid_config.init_lon;
    g_beacon_config.latitude = (float)g_crid_config.init_lat;
    g_beacon_config.longitude = (float)g_crid_config.init_lon;
    g_beacon_config.speed_horizontal = g_crid_config.speed;
    g_beacon_config.patrol_speed = g_crid_config.speed;

    // 3. 初始化 TCP/IP 网络接口与事件循环（乐鑫 Wi-Fi 驱动必需的前置条件）
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 4. 初始化 Wi-Fi 硬件射频驱动，并锁定在配置的目标信道（如 Channel 6）
    ret = crid_wifi_init(g_crid_config.channel, g_beacon_config.ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi RF initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    // 5. 创建 1Hz 的核心无人机 Remote ID 动态模拟发射任务
    BaseType_t task_ret = xTaskCreate(crid_send_beacon_task, "cn_crid_tx_task", 4096, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Critical Error: Failed to create TX task!");
        return;
    }

    // 7. 打印启动成功状态横幅
    ESP_LOGI(TAG, "--------------------------------------------------------");
    ESP_LOGI(TAG, "Transmitter deployed successfully!");
    ESP_LOGI(TAG, "Default Target Channel: %u", g_crid_config.channel);
    ESP_LOGI(TAG, "OUI: FA:0B:BC, Vendor Type: 0x0D (GB42590-2023)");
    ESP_LOGI(TAG, "--------------------------------------------------------");

    // 提示：app_main 此时可以结束执行，FreeRTOS 会自动回收该主线程，
    // 后台的 CLI 任务和 TX 发射任务将在其各自的优先级下持久并发运行。
}