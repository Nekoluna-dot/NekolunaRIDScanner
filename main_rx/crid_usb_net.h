/**
 * crid_usb_net.h — USB NCM 网络接口声明
 */

#ifndef CRID_USB_NET_H
#define CRID_USB_NET_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 USB NCM 网络接口
 * 创建带 DHCP 服务器的虚拟网卡，供主机通过 USB 访问 HTTP 服务
 */
esp_err_t crid_usb_net_init(void);

/**
 * 反初始化 USB NCM 网络接口
 */
void crid_usb_net_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // CRID_USB_NET_H
