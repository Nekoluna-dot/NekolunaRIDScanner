/**
 * crid_ble.h — BLE NUS 数据通道接口
 *
 * 使用 NimBLE 实现 Nordic UART Service (NUS) 外设，
 * 将 JSON 数据流通过 BLE 通知发送到 Android app。
 *
 * 内存优化：NimBLE 内存池分配在 SPIRAM 中。
 */

#ifndef CRID_BLE_H
#define CRID_BLE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 BLE NUS 外设
 * - 注册 NUS 服务 (TX/RX characteristics)
 * - 配置广播名称为 "Nekoluna-Scanner"
 * - 分配 NimBLE 内存池到 SPIRAM
 *
 * @return ESP_OK 成功，否则失败
 */
esp_err_t crid_ble_init(void);

/**
 * BLE JSON 数据写入回调
 * 注册到 crid_json 的数据流，每次有 JSON 输出时调用
 */
void crid_ble_write_cb(const char *data, size_t len, void *ctx);

/**
 * 检查 BLE 是否已连接
 */
bool crid_ble_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // CRID_BLE_H