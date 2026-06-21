/**
 * crid_parser.h — 协议解析模块头文件
 *
 * 包含所有协议相关的函数声明
 */

#ifndef CRID_PARSER_H
#define CRID_PARSER_H

#include "crid_rx_types.h"

/* 字段提取宏：简化 crid_parser_extract_layered */
#define EXTRACT_IF(valid_flag, dst, src) \
    do { if (valid_flag) { (dst) = (src); } } while(0)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 解析并判断协议类型
 * @param uav 无人机追踪结构体
 * @param data 数据指针
 * @param len 数据长度
 * @return 协议类型
 */
rid_protocol_t crid_parser_decode(uav_track_t *uav, const uint8_t *data, uint8_t len);

/**
 * 从原始数据解析 GB 46750 协议
 * @param uav 无人机追踪结构体
 * @param data 数据指针
 * @param len 数据长度
 * @return 是否成功解析
 */
bool crid_parser_decode_gb46750(uav_track_t *uav, const uint8_t *data, uint8_t len);

/**
 * 从原始数据解析 ASTM F3411 协议
 * @param uav 无人机追踪结构体
 * @param data 数据指针
 * @param len 数据长度
 * @return 是否成功解析
 */
bool crid_parser_decode_astm(uav_track_t *uav, const uint8_t *data, uint8_t len);

/**
 * 从原始数据解析 GB 42590 协议
 * @param uav 无人机追踪结构体
 * @param data 数据指针
 * @param len 数据长度
 * @return 是否成功解析
 */
bool crid_parser_decode_gb42590(uav_track_t *uav, const uint8_t *data, uint8_t len);

/**
 * 提取分层数据
 * @param uav 无人机追踪结构体
 */
void crid_parser_extract_layered(uav_track_t *uav);

#ifdef __cplusplus
}
#endif

#endif // CRID_PARSER_H
