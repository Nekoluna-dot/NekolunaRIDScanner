/**
 * crid_json.h — JSON 格式化输出模块接口
 *
 * 提供统一的结构化 JSON 输出，支持：
 *   - 系统启动/状态信息
 *   - 无人机 Remote ID 解析数据
 *   - 调试/告警/错误信息
 *
 * 双输出流设计：
 *   - 数据流（data stream）：UAV 解析数据、状态统计
 *     → 默认 stdout，可通过 json_set_data_stream() 切换到 UART 等
 *   - 调试流（debug stream）：启动信息、调试、告警、错误、解码诊断
 *     → 默认 stdout，可通过 json_set_debug_stream() 切换
 */

#ifndef CRID_JSON_H
#define CRID_JSON_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "crid_rx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 输出流配置
 * ================================================================ */

/**
 * 数据流写入回调类型
 * 用于将 UAV 数据/状态输出到自定义端口（如 UART）
 * @param data  要写入的数据（以 \0 结尾的字符串）
 * @param len   数据长度
 * @param ctx   用户上下文
 */
typedef void (*json_write_cb_t)(const char *data, size_t len, void *ctx);

/**
 * 设置数据流写入回调（UAV 数据、状态统计）
 * 设置后，所有数据类 JSON 输出会调用此回调，同时仍输出到 stdout
 * @param cb  回调函数，传 NULL 仅输出到 stdout
 * @param ctx 用户上下文，传给回调
 */
void json_set_data_write_cb(json_write_cb_t cb, void *ctx);

/**
 * 获取当前数据流回调
 */
json_write_cb_t json_get_data_write_cb(void);

/**
 * 获取当前数据流回调上下文
 */
void *json_get_data_write_ctx(void);

/* ================================================================
 * JSON 事件类型
 * ================================================================ */

typedef enum {
    JSON_EVT_STARTUP,         // 系统启动
    JSON_EVT_STATUS,          // 定期状态统计
    JSON_EVT_UAV_DISCOVERY,   // 新 UAV 发现（摘要）
    JSON_EVT_UAV_UPDATE,      // UAV 数据更新（完整解析数据）
    JSON_EVT_UAV_STATUS,      // UAV 活跃状态行
    JSON_EVT_UAV_TIMEOUT,     // UAV 超时移除
    JSON_EVT_UAV_DETAIL,      // UAV 完整详情（所有字段）
    JSON_EVT_WARNING,         // 告警
    JSON_EVT_ERROR,           // 错误
    JSON_EVT_DEBUG,           // 调试信息
    JSON_EVT_DECODE_FAIL,     // 解码失败诊断
    JSON_EVT_IDLE,            // 无活跃 UAV 标志
} json_event_type_t;

/* ================================================================
 * JSON 转义工具
 * ================================================================ */

/**
 * JSON 字符串转义（内联到缓冲区）
 * 处理 \ " 和控制字符
 * @param dst  目标缓冲区
 * @param src  源字符串
 * @param dst_size  目标缓冲区大小
 * @return 写入的字节数
 */
int json_escape_str(char *dst, const char *src, size_t dst_size);

/* ================================================================
 * JSON 构建函数 — 通用事件
 * ================================================================ */

/**
 * 输出启动横幅 JSON
 */
void json_startup_banner(const char *version, const char *build_date,
                         const char *build_time, uint8_t channel,
                         int max_uavs, uint32_t free_heap);

/**
 * 输出系统启动状态 JSON（ESP-IDF 版本等）
 */
void json_startup_info(const char *version, const char *build_date,
                       const char *build_time, const char *idf_version,
                       uint32_t free_heap, int protocol_version);

/**
 * 输出定期状态统计 JSON
 */
void json_status_report(uint32_t loop_minutes, uint32_t free_heap,
                        uint32_t total_pkts, float pkts_per_sec,
                        uint32_t mgmt_frames, float mgmt_per_sec,
                        uint32_t beacons, float beacons_per_sec,
                        uint32_t rid_detections, float rid_per_sec,
                        uint32_t non_rid_vendor, float non_rid_per_sec,
                        uint32_t queue_overflows, int active_uavs);

/* ================================================================
 * JSON 构建函数 — UAV 相关
 * ================================================================ */

/**
 * 输出新 UAV 发现 JSON（摘要）
 */
void json_uav_discovery(const uav_track_t *uav);

/**
 * 输出 UAV 完整解析数据 JSON（每次解码后）
 */
void json_uav_update(const uav_track_t *uav);

/**
 * 输出 UAV 活跃状态行 JSON（monitor 列表）
 */
void json_uav_status(const uav_track_t *uav);

/**
 * 输出 UAV 完整详情 JSON（所有字段，含 Auth 等）
 */
void json_uav_detail(const uav_track_t *uav);

/**
 * 输出 UAV 超时移除 JSON
 */
void json_uav_timeout(const uint8_t *mac);

/**
 * 输出无活跃无人机标志 JSON（每 1s 输出一次）
 */
void json_no_aircraft(void);

/* ================================================================
 * JSON 构建函数 — 日志/诊断
 * ================================================================ */

/**
 * 输出告警 JSON
 * @param module  模块名（如 "RID_SNIFF", "RID_PARSE" 等）
 * @param message 告警消息
 */
void json_warning(const char *module, const char *message);

/**
 * 输出错误 JSON
 * @param module  模块名
 * @param message 错误消息
 */
void json_error(const char *module, const char *message);

/**
 * 输出调试 JSON
 * @param module  模块名
 * @param message 调试消息
 */
void json_debug(const char *module, const char *message);

/**
 * 输出解码失败诊断 JSON
 */
void json_decode_fail(uint8_t byte0, uint8_t byte1, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif // CRID_JSON_H
