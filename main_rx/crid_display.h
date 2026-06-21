/**
 * crid_display.h — 无人机信息展示模块接口
 */

#ifndef CRID_DISPLAY_H
#define CRID_DISPLAY_H

#include "crid_rx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MAC 地址转字符串
 */
void crid_display_mac_str(const uint8_t *mac, char *buf, size_t size);

/**
 * 打印无人机发现摘要（精简 1 行，新 UAV 首次发现时调用）
 */
void crid_display_uav_summary(const uav_track_t *uav);

/**
 * 打印无人机详细解析信息（所有字段）
 */
void crid_display_uav_detail(const uav_track_t *uav);

/**
 * 打印活跃无人机状态行（用于 monitor 列表）
 */
void crid_display_uav_status(const uav_track_t *uav);

/**
 * 枚举值 → 名称映射（供外部模块使用）
 */
const char *crid_display_transport_name(uint8_t t);
const char *crid_display_protocol_name(uint8_t p);
const char *crid_display_ua_type_name(uint8_t t);
const char *crid_display_status_name(uint8_t s);

#ifdef __cplusplus
}
#endif

#endif // CRID_DISPLAY_H
