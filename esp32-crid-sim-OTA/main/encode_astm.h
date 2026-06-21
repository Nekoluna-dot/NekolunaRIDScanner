#ifndef CRID_MESSAGES_H
#define CRID_MESSAGES_H

#include "rid_config.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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
