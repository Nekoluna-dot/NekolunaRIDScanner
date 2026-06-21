#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rid_wifi.h"

static const char *TAG = "RID_WIFI";

// 【新增】定义 AP 的接入密码 (WPA2 要求密码长度必须在 8~63 个字符之间)
#define AP_DEFAULT_PASSWORD "12345678"

esp_err_t crid_wifi_init(uint8_t channel, const char *ssid) {
    esp_err_t ret;
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    esp_netif_create_default_wifi_ap();
    
    wifi_config_t ap_config = { 0 };
    const char *ap_ssid = (ssid != NULL && ssid[0] != '\0') ? ssid : "NekolunaRID-OTA";
    
    snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "%s", ap_ssid);
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    ap_config.ap.channel = channel;
    
    // 【修改】将加密模式从 OPEN 改为 WPA2_PSK
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    
    // 【新增】设置 AP 密码
    strncpy((char *)ap_config.ap.password, AP_DEFAULT_PASSWORD, sizeof(ap_config.ap.password) - 1);
    
    ap_config.ap.max_connection = 4;
    ap_config.ap.beacon_interval = 100;
    
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 降低 TX 功率以减少峰值电流（避免 C3 USB 供电欠压）
    esp_wifi_set_max_tx_power(20);  // 20 = 5 dBm

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 等待射频稳定
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_wifi_set_max_tx_power(20);
    
    ret = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_wifi_set_promiscuous(true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_promiscuous failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Wi-Fi initialized: AP mode, channel=%u, SSID=%s, Password=%s", 
             channel, ap_config.ap.ssid, AP_DEFAULT_PASSWORD);
    return ESP_OK;
}

esp_err_t crid_wifi_send_raw_frame(const uint8_t *frame, uint16_t len) {
    if (frame == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = esp_wifi_80211_tx(WIFI_IF_AP, frame, len, false);
    if (ret == ESP_OK) {
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "esp_wifi_80211_tx(AP) failed: %s", esp_err_to_name(ret));
    return ret;
}