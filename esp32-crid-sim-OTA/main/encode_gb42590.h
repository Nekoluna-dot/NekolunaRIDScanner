#ifndef RID_GB42590_H
#define RID_GB42590_H

#include "rid_config.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- 报文常量 ---
#define CRID_MESSAGE_SIZE      25     // 每条报文 25 字节
#define CRID_UAS_ID_MAX_LEN    20     // UAS ID 最大长度

// --- 中国 C-RID 标准 OUI 和类型 ---
#define CRID_OUI_0 0xFA
#define CRID_OUI_1 0x0B
#define CRID_OUI_2 0xBC
#define CRID_VENDOR_TYPE 0x0D

// --- 报文类型 (符合 ASTM F3411 / ASD-STAN 4709-002) ---
#define MSG_TYPE_BASIC_ID    0x0  // 基本 ID 报文
#define MSG_TYPE_LOCATION    0x1  // 位置向量报文
#define MSG_TYPE_AUTH        0x2  // 认证报文
#define MSG_TYPE_SELF_DESC   0x3  // 运行描述报文
#define MSG_TYPE_SYSTEM      0x4  // 系统报文
#define MSG_TYPE_OPERATOR_ID 0x5  // 操作员 ID 报文
#define MSG_TYPE_PACKED      0xF  // 报文打包

// --- 描述类型 ---
#define DESC_TYPE_TEXT            0
#define DESC_TYPE_EMERGENCY       1
#define DESC_TYPE_EXTENDED_STATUS 2

/**
 * @brief 构建 Basic ID 报文 (25 字节，符合试行标准表3)
 * @param config 配置结构体指针
 * @param message 输出缓冲区 (至少 25 字节)
 */
void crid_build_basic_id_message(const cn_crid_config_t *config, uint8_t *message);

/**
 * @brief 构建 Location 报文 (25 字节，符合试行标准表4)
 * @param config 配置结构体指针
 * @param message 输出缓冲区 (至少 25 字节)
 */
void crid_build_location_message(const cn_crid_config_t *config, uint8_t *message);

/**
 * @brief 构建 System 报文 (25 字节，符合试行标准表6)
 * @param config 配置结构体指针
 * @param message 输出缓冲区 (至少 25 字节)
 */
void crid_build_system_message(const cn_crid_config_t *config, uint8_t *message);

/**
 * @brief 构建 Self-Description 报文 (25 字节，符合试行标准表5)
 * @param config 配置结构体指针
 * @param message 输出缓冲区 (至少 25 字节)
 */
void crid_build_self_desc_message(const cn_crid_config_t *config, uint8_t *message);

/**
 * @brief 构建 Authentication 报文 (25 字节，符合 ASTM F3411)
 * @param config 配置结构体指针
 * @param message 输出缓冲区 (至少 25 字节)
 */
void crid_build_auth_message(const cn_crid_config_t *config, uint8_t *message);

/**
 * @brief 构建 Operator ID 报文 (25 字节，符合 ASTM F3411)
 * @param config 配置结构体指针
 * @param message 输出缓冲区 (至少 25 字节)
 */
void crid_build_operator_id_message(const cn_crid_config_t *config, uint8_t *message);

/**
 * @brief 构建完整的 Beacon 帧 (包含打包报文)
 * @param config 配置结构体指针
 * @param frame 输出帧缓冲区
 * @param max_len 缓冲区最大长度
 * @param[out] out_len 实际帧长度
 * @return true 成功, false 缓冲区不足
 */
bool crid_build_beacon_frame(const cn_crid_config_t *config,
                             uint8_t message_counter,
                             uint8_t *frame, uint16_t max_len,
                             uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // CRID_MESSAGES_H
