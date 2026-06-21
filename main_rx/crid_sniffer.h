/**
 * crid_sniffer.h — Wi-Fi Sniffer 模块接口
 */

#ifndef CRID_SNIFFER_H
#define CRID_SNIFFER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "crid_rx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 获取 sniffer 消息队列句柄
 */
QueueHandle_t crid_sniffer_get_queue(void);

/**
 * 获取全局统计指针
 */
sniffer_stats_t *crid_sniffer_get_stats(void);

/**
 * 初始化 Wi-Fi 监控模式（NULL mode + Promiscuous）
 * @return ESP_OK 成功，否则失败
 */
esp_err_t crid_sniffer_init(void);

/**
 * 启动信道保持任务（锁定信道，防止漂移）
 */
void crid_sniffer_start_channel_hold(void);

#ifdef __cplusplus
}
#endif

#endif // CRID_SNIFFER_H
