#ifndef CRID_WIFI_H
#define CRID_WIFI_H

#include "rid_config.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 Wi-Fi 用于 raw 802.11 帧发送
 * @param channel Wi-Fi 信道
 * @return ESP_OK 成功
 */
esp_err_t crid_wifi_init(uint8_t channel, const char *ssid);

/**
 * @brief 发送 raw 802.11 帧，带 fallback 机制
 * @param frame 帧数据
 * @param len 帧长度
 * @return ESP_OK 成功
 */
esp_err_t crid_wifi_send_raw_frame(const uint8_t *frame, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // CRID_WIFI_H
