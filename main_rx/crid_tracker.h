/**
 * crid_tracker.h — 无人机追踪表接口（线程安全）
 */

#ifndef CRID_TRACKER_H
#define CRID_TRACKER_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "crid_rx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化追踪表
 */
void crid_tracker_init(void);

/**
 * 获取追踪互斥锁句柄
 */
SemaphoreHandle_t crid_tracker_get_mutex(void);

/**
 * 通过 MAC 地址查找或创建追踪条目
 * @param mac  6 字节 MAC 地址
 * @return 追踪条目指针，表满返回 NULL
 */
uav_track_t *crid_tracker_find_or_create(const uint8_t *mac);

/**
 * 获取活跃无人机数量
 */
int crid_tracker_get_active_count(void);

/**
 * 获取追踪表数组（用于遍历）
 */
uav_track_t *crid_tracker_get_table(void);

/**
 * 清理超时条目
 * @param timeout_ms  超时阈值（毫秒）
 */
void crid_tracker_cleanup(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // CRID_TRACKER_H
