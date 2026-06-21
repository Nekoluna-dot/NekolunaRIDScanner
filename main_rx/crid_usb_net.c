/**
 * crid_usb_net.c — USB NCM 网络接口
 *
 * 创建 USB NCM 虚拟网卡，提供 DHCP 服务器，供主机通过 USB 访问 HTTP 服务。
 * 不影响 Wi-Fi sniffer 的 NULL 模式。
 *
 * Note: Only compiled on chips with USB OTG support (ESP32-S2/S3/P4).
 *       Not available on ESP32-C3/C5/C6/H2.
 */

#include "soc/soc_caps.h"
#if SOC_USB_OTG_SUPPORTED

#include <string.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_mac.h>
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_net.h"
#include "lwip/esp_netif_net_stack.h"
#include "dhcpserver/dhcpserver_options.h"

#include "crid_usb_net.h"

static const char *TAG = "C-RID USB";

static esp_netif_t *s_netif = NULL;

/* USB NCM 网络的 IP 配置：设备 192.168.7.1，DHCP 分配 192.168.7.x */
static const esp_netif_ip_info_t s_usb_ip_info = {
    .ip      = { .addr = ESP_IP4TOADDR(192, 168, 7, 1) },
    .gw      = { .addr = ESP_IP4TOADDR(192, 168, 7, 1) },
    .netmask = { .addr = ESP_IP4TOADDR(255, 255, 255, 0) },
};

/* ---------- L2 收发回调 ---------- */

static void l2_free(void *h, void *buffer)
{
    free(buffer);
}

static esp_err_t netif_transmit(void *h, void *buffer, size_t len)
{
    if (tinyusb_net_send_sync(buffer, len, NULL, pdMS_TO_TICKS(100)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send buffer to USB!");
    }
    return ESP_OK;
}

static esp_err_t netif_recv_callback(void *buffer, uint16_t len, void *ctx)
{
    if (s_netif) {
        void *buf_copy = malloc(len);
        if (!buf_copy) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(buf_copy, buffer, len);
        return esp_netif_receive(s_netif, buf_copy, len, NULL);
    }
    return ESP_OK;
}

/* ---------- 公共接口 ---------- */

esp_err_t crid_usb_net_init(void)
{
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    /* USB NCM 设备 MAC（主机看到的虚拟网卡 MAC） */
    const tinyusb_net_config_t net_config = {
        .mac_addr = {0x02, 0x02, 0x11, 0x22, 0x33, 0x01},
        .on_recv_callback = netif_recv_callback,
    };

    esp_err_t ret = tinyusb_net_init(&net_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cannot initialize USB Net device");
        return ret;
    }

    /* ESP32 lwIP 侧 MAC（必须与 USB NCM 设备 MAC 不同） */
    uint8_t lwip_addr[6] = {0x02, 0x02, 0x11, 0x22, 0x33, 0x02};

    /* 创建带 DHCP 服务器的 netif（配置类似 SoftAP） */
    esp_netif_inherent_config_t base_cfg = {
        .flags = ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_AUTOUP,
        .ip_info = &s_usb_ip_info,
        .if_key = "wired",
        .if_desc = "usb ncm config device",
        .route_prio = 10
    };

    esp_netif_driver_ifconfig_t driver_cfg = {
        .handle = (void *)1,
        .transmit = netif_transmit,
        .driver_free_rx_buffer = l2_free
    };

    struct esp_netif_netstack_config lwip_netif_config = {
        .lwip = {
            .init_fn = ethernetif_init,
            .input_fn = ethernetif_input
        }
    };

    esp_netif_config_t cfg = {
        .base = &base_cfg,
        .driver = &driver_cfg,
        .stack = &lwip_netif_config
    };

    s_netif = esp_netif_new(&cfg);
    if (s_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create netif");
        return ESP_FAIL;
    }
    esp_netif_set_mac(s_netif, lwip_addr);

    /* 设置最短租约时间 */
    uint32_t lease_opt = 1;
    esp_netif_dhcps_option(s_netif, ESP_NETIF_OP_SET, IP_ADDRESS_LEASE_TIME,
                           &lease_opt, sizeof(lease_opt));

    /* 启动接口 */
    esp_netif_action_start(s_netif, 0, 0, 0);

    ESP_LOGI(TAG, "USB NCM network interface started");
    ESP_LOGI(TAG, "Device IP: 192.168.7.1, Host will get: 192.168.7.x (via DHCP)");

    return ESP_OK;
}

void crid_usb_net_deinit(void)
{
    if (s_netif) {
        esp_netif_action_stop(s_netif, 0, 0, 0);
        esp_netif_destroy(s_netif);
        s_netif = NULL;
        ESP_LOGI(TAG, "USB NCM network interface stopped");
    }
}

#endif /* SOC_USB_OTG_SUPPORTED */
