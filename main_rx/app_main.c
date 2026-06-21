/**
 * crid_scan_main.c — Remote ID Scanner 主入口
 *
 * ESP32 Remote ID Scanner
 * Standards: ASTM F3411-22a / ASD-STAN prEN 4709-002 / GB 42590-2023 / GB 46750-2025
 *
 * 架构：
 *   - crid_sniffer:   Wi-Fi 混杂模式抓包，ISR 安全回调
 *   - crid_parser:    opendroneid 库解码
 *   - crid_tracker:   无人机追踪表（线程安全）
 *   - crid_display:   信息展示（摘要/详情/状态行）
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "driver/uart.h"
#include "opendroneid.h"
#include "esp_wifi.h"

#include "crid_rx_types.h"
#include "crid_sniffer.h"
#include "crid_parser.h"
#include "crid_tracker.h"
#include "crid_display.h"
#include "crid_json.h"
#include "crid_ble.h"

#ifndef CRID_VERSION_STRING
#define CRID_VERSION_STRING "1.0.0"
#endif
#ifndef CRID_BUILD_DATE
#define CRID_BUILD_DATE     __DATE__
#endif
#ifndef CRID_BUILD_TIME
#define CRID_BUILD_TIME     __TIME__
#endif

/* ================================================================
 * UART 数据端口配置
 *
 * 硬件连接：ESP32-S3 UART1
 *   - TX: GPIO17
 *   - RX: GPIO18 (不使用)
 *   - 波特率: 115200
 *
 * 此端口仅输出 UAV 解析数据（uav_discovery / uav_update / uav_status
 * / uav_timeout / uav_detail / status），方便上位机接收纯净数据流。
 *
 * 调试/告警/错误/启动信息仍通过 USB CDC (stdout) 输出。
 * ================================================================ */

#define UART_DATA_PORT_NUM      UART_NUM_1
#define UART_DATA_TX_PIN        17
#define UART_DATA_RX_PIN        18
#define UART_DATA_BAUD_RATE     115200
#define UART_DATA_BUF_SIZE      1024

static bool s_uart_data_initialized = false;

static void uart_data_port_init(void) {
#if CONFIG_IDF_TARGET_ESP32C5
    return;
#endif
    uart_config_t uart_config = {
        .baud_rate = UART_DATA_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_DATA_PORT_NUM, &uart_config);
    uart_set_pin(UART_DATA_PORT_NUM, UART_DATA_TX_PIN, UART_DATA_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_DATA_PORT_NUM, UART_DATA_BUF_SIZE, 0, 0, NULL, 0);
    s_uart_data_initialized = true;
}

/**
 * UART 数据写入回调：将 JSON 数据同时写到 UART1
 */
static void uart_data_write_cb(const char *data, size_t len, void *ctx) {
    (void)ctx;
    if (!s_uart_data_initialized) return;
    uart_write_bytes(UART_DATA_PORT_NUM, data, len);
}

/**
 * 数据流扇出回调：写入 UART1 + BLE
 */
static void data_write_fanout(const char *data, size_t len, void *ctx) {
    uart_data_write_cb(data, len, ctx);
    crid_ble_write_cb(data, len, ctx);
}

/* ================================================================
 * 解析任务 (从队列取数据，使用 opendroneid 库解析)
 * ================================================================ */

static void parser_task(void *pvParameter) {
    json_debug("RID_MAIN", "Parser task started");

    QueueHandle_t queue = crid_sniffer_get_queue();
    SemaphoreHandle_t mutex = crid_tracker_get_mutex();
    sniffer_msg_t msg;
    uint32_t last_cleanup_ms = 0;
    uint32_t last_idle_output_ms = 0;

    while (1) {
        if (xQueueReceive(queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
            uint32_t now = esp_log_timestamp();
            if (crid_tracker_get_active_count() == 0 &&
                (now - last_idle_output_ms >= 1000)) {
                json_no_aircraft();
                last_idle_output_ms = now;
            }
            continue;
        }

        // 普通 Beacon（无 Vendor IE）：直接跳过
        if (msg.msg_type == MSG_TYPE_BEACON_NO_VENDOR) {
            continue;
        }

        // 非 RID Vendor IE：直接跳过
        if (msg.msg_type == MSG_TYPE_NON_RID_VENDOR) {
            continue;
        }

        // 以下仅处理 MSG_TYPE_RID

        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        uav_track_t *uav = crid_tracker_find_or_create(msg.src_mac);
        if (uav == NULL) {
            // 追踪表已满，尝试清理超时条目腾出空间
            uint32_t now = esp_log_timestamp();
            if (now - last_cleanup_ms >= 10000) {  // 每 10 秒最多清理一次
                crid_tracker_cleanup(UAV_TIMEOUT_MS);
                last_cleanup_ms = now;
                // 重试一次
                uav = crid_tracker_find_or_create(msg.src_mac);
            }
            if (uav == NULL) {
                json_warning("RID_MAIN", "Tracker full! Cannot track new UAV");
                xSemaphoreGive(mutex);
                continue;
            }
        }

        bool was_new = (uav->msg_count == 0);
        uav->msg_count++;
        uav->last_rssi = msg.rssi;
        uav->last_channel = msg.channel;

        // 记录 OUI 和传输/协议类型
        uav->oui[0] = msg.oui[0];
        uav->oui[1] = msg.oui[1];
        uav->oui[2] = msg.oui[2];
        uav->oui_type = msg.oui_type;
        uav->transport = (uint8_t)GET_RID_TRANSPORT(msg.oui[0], msg.oui[1], msg.oui[2]);

        // 解码（解析器内部自动识别协议并返回）
        rid_protocol_t detected_proto = crid_parser_decode(uav, msg.data, msg.data_len);

        // 仅解码成功时更新协议类型，失败则保持上次已知的协议
        if (detected_proto != RID_PROTOCOL_UNKNOWN) {
            uav->protocol = (uint8_t)detected_proto;
        }

        // 提取分层数据（供显示层使用）
        crid_parser_extract_layered(uav);

        xSemaphoreGive(mutex);

        // 新发现 UAV 时输出发现事件 + 完整更新
        if (was_new && uav->basic_id.valid) {
            json_uav_discovery(uav);
        }
        // 每次解码后输出完整解析数据
        if (uav->basic_id.valid) {
            json_uav_update(uav);
        }
    }
}


static void monitor_task(void *pvParameter) {
    json_debug("RID_MAIN", "Monitor task started");

    uint32_t loop_count = 0;
    uint32_t last_packets = 0;
    uint32_t last_mgmt = 0;
    uint32_t last_rid = 0;
    uint32_t last_beacons = 0;
    uint32_t last_non_rid = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        loop_count++;

        sniffer_stats_t *stats = crid_sniffer_get_stats();
        SemaphoreHandle_t mutex = crid_tracker_get_mutex();

        uint32_t total_pkts = stats->total_packets;
        uint32_t mgmt_pkts = stats->mgmt_frames;
        uint32_t rid_pkts = stats->rid_detections;
        uint32_t overflows = stats->queue_overflows;
        uint32_t beacons = stats->beacon_count;
        uint32_t non_rid = stats->non_rid_vendor_ie;

        float pkt_rate = (total_pkts - last_packets) / 60.0f;
        float mgmt_rate = (mgmt_pkts - last_mgmt) / 60.0f;
        float beacon_rate = (beacons - last_beacons) / 60.0f;
        float rid_rate = (rid_pkts - last_rid) / 60.0f;
        float non_rid_rate = (non_rid - last_non_rid) / 60.0f;

        last_packets = total_pkts;
        last_mgmt = mgmt_pkts;
        last_rid = rid_pkts;
        last_beacons = beacons;
        last_non_rid = non_rid;

        // 打印活跃无人机列表并清理超时
        int active = 0;
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            active = crid_tracker_get_active_count();

            uav_track_t *table = crid_tracker_get_table();
            for (int i = 0; i < MAX_TRACKED_UAVS; i++) {
                if (!table[i].active) continue;
                json_uav_status(&table[i]);
            }

            // 清理超时条目
            crid_tracker_cleanup(UAV_TIMEOUT_MS);

            xSemaphoreGive(mutex);
        }

        // 输出汇总状态 JSON
        json_status_report(loop_count,
                          (uint32_t)esp_get_free_heap_size(),
                          total_pkts, pkt_rate,
                          mgmt_pkts, mgmt_rate,
                          beacons, beacon_rate,
                          rid_pkts, rid_rate,
                          non_rid, non_rid_rate,
                          overflows, active);
    }
}

/* ================================================================
 * 主函数
 * ================================================================ */

void app_main(void) {
    // 0. 初始化 UART 数据端口（GPIO17 TX），用于输出 UAV 解析数据
    uart_data_port_init();

    // 设置数据流回调：UAV 数据同时输出到 stdout（USB CDC）和 UART1
    json_set_data_write_cb(uart_data_write_cb, NULL);

    // JSON 启动横幅（调试流 → 仅 USB CDC）
    json_startup_banner(CRID_VERSION_STRING, CRID_BUILD_DATE, CRID_BUILD_TIME,
                        FIXED_CHANNEL, MAX_TRACKED_UAVS,
                        (uint32_t)esp_get_free_heap_size());

    // JSON 启动信息（调试流 → 仅 USB CDC）
    json_startup_info(CRID_VERSION_STRING, CRID_BUILD_DATE, CRID_BUILD_TIME,
                      esp_get_idf_version(),
                      (uint32_t)esp_get_free_heap_size(),
                      ODID_PROTOCOL_VERSION);

    // 1. 初始化追踪器
    crid_tracker_init();

    // 2. 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        json_warning("RID_MAIN", "Erasing NVS flash...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        char err[64];
        snprintf(err, sizeof(err), "NVS init failed: %s", esp_err_to_name(ret));
        json_error("RID_MAIN", err);
        return;
    }

    // 3. 初始化网络接口和事件循环
    esp_netif_init();
    esp_event_loop_create_default();

    // 4. 初始化 Wi-Fi sniffer
    ret = crid_sniffer_init();
    if (ret != ESP_OK) {
        json_error("RID_MAIN", "Sniffer init failed!");
        return;
    }

    // 5. 初始化 BLE NUS (NimBLE，内存分配在 SPIRAM)
    ret = crid_ble_init();
    if (ret != ESP_OK) {
        json_warning("RID_MAIN", "BLE init failed (non-fatal)");
    } else {
        // BLE 就绪后切换为扇出回调：UART + BLE 双路输出
        json_set_data_write_cb(data_write_fanout, NULL);
        json_debug("RID_MAIN", "BLE data channel enabled (SPIRAM)");
    }

    // 6. 创建任务
    BaseType_t task_created;

    task_created = xTaskCreate(parser_task, "parser",
                               PARSER_TASK_STACK, NULL, PARSER_TASK_PRIO, NULL);
    if (task_created != pdPASS) {
        json_error("RID_MAIN", "Failed to create parser task!");
        return;
    }

    task_created = xTaskCreate(monitor_task, "monitor",
                               MONITOR_TASK_STACK, NULL, MONITOR_TASK_PRIO, NULL);
    if (task_created != pdPASS) {
        json_error("RID_MAIN", "Failed to create monitor task!");
        return;
    }

    crid_sniffer_start_channel_hold();

    // 8. 启动完成（调试流 → USB CDC）
    json_startup_banner(CRID_VERSION_STRING, CRID_BUILD_DATE, CRID_BUILD_TIME,
                        FIXED_CHANNEL, MAX_TRACKED_UAVS,
                        (uint32_t)esp_get_free_heap_size());
}
