/**
 * crid_sniffer.c — Wi-Fi Sniffer 模块 (稳定版)
 * 
 * 修复：
 *   - ISR 中使用 xQueueSendFromISR
 *   - decodeMessagePack 调用修正（正确指针）
 *   - 修正 ODID payload 偏移（跳过 Message Counter）
 *   - IE 遍历结构化跳转
 *   - 移除 ISR 中的日志
 *   - 使用 xTaskGetTickCountFromISR
 *   - 消除 type-limits 警告（直接赋值）
 */

#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi_types.h"
#include "freertos/task.h"
#include "crid_sniffer.h"
#include "crid_json.h"
#include "opendroneid.h"

/* ================================================================
 MAC 地址调试过滤器
 ================================================================ */
#define DEBUG_MAC_FILTER_ENABLED    0   
#ifndef DEBUG_MAC_FILTER_ENABLED
static const uint8_t debug_target_mac[6] = {
    0x24, 0x0A, 0xC4, 0x12, 0x34, 0x56  
};
#define DEBUG_MAC_PREFIX_LEN        6   
#endif

/* ---- 模块内部状态 ---- */
static QueueHandle_t g_sniffer_queue = NULL;
static sniffer_stats_t g_stats;
int8_t g_rid_min_rssi = RID_MIN_RSSI_DEFAULT;
static const char *TAG = "WIFI_SNIFFER";

/* ---- 固定信道（需在项目配置中定义） ---- */
#ifndef FIXED_CHANNEL
#define FIXED_CHANNEL 6
#endif

/* ================================================================
 Debug 辅助 (仅在任务/初始化中使用)
 ================================================================ */
#if PARSER_DEBUG_HEX_DUMP
static void hex_dump(const char *tag, const char *prefix, const uint8_t *data, uint8_t len) {
    char line[128];
    int pos = 0;
    pos += snprintf(line + pos, sizeof(line) - pos, "%s [%u]  ", prefix, len);
    for (int i = 0; i < len; i++) {
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", data[i]);
        if ((i + 1) % 16 == 0 && i + 1 < len) {
            ESP_LOGI(tag, "%s", line);
            pos = snprintf(line, sizeof(line), "        ");
        }
    }
    if (pos > 0) ESP_LOGI(tag, "%s", line);
}
#endif

/* ================================================================
 ISR 回调
 ================================================================ */
static void wifi_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    g_stats.total_packets++;
    if (type != WIFI_PKT_MGMT) return;
    g_stats.mgmt_frames++;

    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint16_t actual_len = pkt->rx_ctrl.sig_len;   // 不含 FCS

    if (actual_len < 36) return;

    wifi_mac_hdr_t *hdr = (wifi_mac_hdr_t *)pkt->payload;

    uint8_t type_field = (hdr->frame_ctrl & 0x0C) >> 2;
    uint8_t subtype    = (hdr->frame_ctrl & 0xF0) >> 4;
    if (type_field != 0 || (subtype != 8 && subtype != 5)) return;

    if (subtype == 8) g_stats.beacon_count++;

    uint8_t *ie_ptr = pkt->payload + 36;
    uint16_t ie_total_len = actual_len - 36;

    uint8_t ssid[33] = {0};
    uint8_t ssid_len = 0;
    bool has_rid_ie = false;
    uint16_t i = 0;

    // ---------- 结构化 IE 遍历 ----------
    while (i + 2 <= ie_total_len) {
        uint8_t id  = ie_ptr[i];
        uint8_t len = ie_ptr[i + 1];
        if (i + 2 + len > ie_total_len) break;

        // 处理 Vendor IE (ID=0xDD)
        if (id == 0xDD) {
            if (len >= 5) {  // OUI(3) + Type(1) + Counter(1) 至少
                uint8_t oui0 = ie_ptr[i + 2];
                uint8_t oui1 = ie_ptr[i + 3];
                uint8_t oui2 = ie_ptr[i + 4];
                uint8_t oui_type = ie_ptr[i + 5];

                if (IS_RID_OUI(oui0, oui1, oui2) && oui_type == 0x0D) {
                    has_rid_ie = true;
                    g_stats.rid_detections++;

                    // 跳过弱信号
                    if (pkt->rx_ctrl.rssi < g_rid_min_rssi) {
                        break;
                    }

                    // 【修正】跳过 ID+Len+OUI+Type+Counter = 7 字节
                    uint8_t *odid_payload = &ie_ptr[i + 7];
                    // 有效数据长度 = Len - 5 (OUI+Type+Counter)
                    uint8_t odid_len = len - 5;

                    // 调用 OpenDroneID 库解码
                    ODID_UAS_Data uas_data;
                    memset(&uas_data, 0, sizeof(uas_data));
                    int decode_ret = decodeMessagePack(&uas_data, (ODID_MessagePack_encoded *)odid_payload);

                    if (decode_ret >= 0) {
                        sniffer_msg_t msg;
                        memset(&msg, 0, sizeof(msg));

                        memcpy(msg.src_mac, hdr->addr2, 6);
                        msg.rssi = pkt->rx_ctrl.rssi;
                        msg.channel = pkt->rx_ctrl.channel;
                        msg.timestamp_ms = (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS);
                        msg.is_rid = true;
                        msg.msg_type = MSG_TYPE_RID;
                        msg.oui[0] = oui0; msg.oui[1] = oui1; msg.oui[2] = oui2;
                        msg.oui_type = oui_type;
                        msg.has_vendor_ie = true;

                        memcpy(msg.ssid, ssid, ssid_len);
                        msg.ssid_len = ssid_len;

                        // 复制 ODID 原始数据（odid_len 最大 250，msg.data 至少 256 字节，安全）
                        uint16_t copy_len = odid_len;
                        memcpy(msg.data, odid_payload, copy_len);
                        msg.data_len = copy_len;

                        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                        if (xQueueSendFromISR(g_sniffer_queue, &msg, &xHigherPriorityTaskWoken) != pdTRUE) {
                            g_stats.queue_overflows++;
                        }
                        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
                    }
                    // 解码失败不统计，避免干扰
                    break;  // 一个帧只应有一个 RID IE
                } else {
                    g_stats.non_rid_vendor_ie++;
                }
            }
        }
        // 处理 SSID IE (ID=0x00)
        else if (id == 0x00 && ssid_len == 0) {
            if (len <= 32) {
                ssid_len = len;
                if (ssid_len > 0) {
                    memcpy(ssid, &ie_ptr[i + 2], ssid_len);
                }
            }
        }

        i += 2 + len;
    }

    // ---------- 无 Vendor IE 的 Beacon 采样 ----------
    static uint32_t s_beacon_no_vendor_count = 0;
    if (subtype == 8 && !has_rid_ie) {
        s_beacon_no_vendor_count++;
        if ((s_beacon_no_vendor_count & 0x7F) == 0) {
            sniffer_msg_t msg;
            memset(&msg, 0, sizeof(msg));
            memcpy(msg.src_mac, hdr->addr2, 6);
            msg.rssi = pkt->rx_ctrl.rssi;
            msg.channel = pkt->rx_ctrl.channel;
            msg.timestamp_ms = (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS);
            msg.is_rid = false;
            msg.msg_type = MSG_TYPE_BEACON_NO_VENDOR;
            msg.has_vendor_ie = false;
            memcpy(msg.ssid, ssid, ssid_len);
            msg.ssid_len = ssid_len;

            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            if (xQueueSendFromISR(g_sniffer_queue, &msg, &xHigherPriorityTaskWoken) != pdTRUE) {
                g_stats.queue_overflows++;
            }
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
}

/* ---- 信道保持任务 ---- */
static void channel_hold_task(void *pvParameter) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Channel hold started, locked to channel %d", FIXED_CHANNEL);
    json_debug("RID_SNIFF", msg);

    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        esp_err_t ret = esp_wifi_set_channel(FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
        if (ret != ESP_OK) {
            snprintf(msg, sizeof(msg), "Failed to set channel %d: %s", FIXED_CHANNEL, esp_err_to_name(ret));
            json_warning("RID_SNIFF", msg);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/* ---- 公开接口 ---- */
QueueHandle_t crid_sniffer_get_queue(void) {
    return g_sniffer_queue;
}

sniffer_stats_t *crid_sniffer_get_stats(void) {
    return &g_stats;
}

esp_err_t crid_sniffer_init(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    char err[64];

    ESP_LOGI(TAG, "Initializing Wi-Fi Sniffer...");

    g_sniffer_queue = xQueueCreate(SNIFFER_QUEUE_SIZE, sizeof(sniffer_msg_t));
    if (g_sniffer_queue == NULL) {
        json_error("RID_SNIFF", "Failed to create sniffer queue!");
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        snprintf(err, sizeof(err), "Wi-Fi init failed: %s", esp_err_to_name(ret));
        json_error("RID_SNIFF", err);
        return ret;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_NULL);
    if (ret != ESP_OK) {
        snprintf(err, sizeof(err), "Wi-Fi mode failed: %s", esp_err_to_name(ret));
        json_error("RID_SNIFF", err);
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        snprintf(err, sizeof(err), "Wi-Fi start failed: %s", esp_err_to_name(ret));
        json_error("RID_SNIFF", err);
        return ret;
    }

    esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb);

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_ALL
    };
    esp_wifi_set_promiscuous_filter(&filter);

    ret = esp_wifi_set_promiscuous(true);
    if (ret != ESP_OK) {
        snprintf(err, sizeof(err), "Promiscuous mode failed: %s", esp_err_to_name(ret));
        json_error("RID_SNIFF", err);
        return ret;
    }

    ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret != ESP_OK) {
        snprintf(err, sizeof(err), "Wi-Fi PS set failed: %s", esp_err_to_name(ret));
        json_warning("RID_SNIFF", err);
    }

    ret = esp_wifi_set_channel(FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        snprintf(err, sizeof(err), "Set channel failed: %s", esp_err_to_name(ret));
        json_error("RID_SNIFF", err);
        return ret;
    }

    snprintf(err, sizeof(err), "Wi-Fi monitor mode enabled, promiscuous ON, channel %d", FIXED_CHANNEL);
    json_debug("RID_SNIFF", err);

    return ESP_OK;
}

void crid_sniffer_start_channel_hold(void) {
    xTaskCreate(channel_hold_task, "ch_hold",
                CH_HOLD_TASK_STACK, NULL, CH_HOLD_TASK_PRIO, NULL);
}