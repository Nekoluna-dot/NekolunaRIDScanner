#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rid_config.h"

static const char *TAG = "CRID_CLI";
#define BUF_SIZE (256)

static void cli_task(void *pvParameters) {
    char *data = (char *) malloc(BUF_SIZE);
    if (data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate CLI buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "CLI Engine started. Usage: SET <lat> <lon> <mode>");
    ESP_LOGI(TAG, "Modes: 0=Circle, 1=PingPong, 2=S-Search. Example: SET 22.54 114.05 1");

    while (1) {
        if (fgets(data, BUF_SIZE, stdin) == NULL) {
            continue;
        }

        char *pos;
        if ((pos = strchr(data, '\n')) != NULL) *pos = '\0';
        if ((pos = strchr(data, '\r')) != NULL) *pos = '\0';

        char cmd[16];
        double t_lat = 0;
        double t_lon = 0;
        int t_mode = 0;

        if (sscanf(data, "%15s %lf %lf %d", cmd, &t_lat, &t_lon, &t_mode) == 4) {
            if (strcmp(cmd, "SET") == 0 && t_mode >= 0 && t_mode < FLIGHT_MODE_MAX) {
                // 动态更新内存全局变量
                crid_dynamic_config_t config_snapshot;
                if (g_crid_config_mutex != NULL) {
                    xSemaphoreTake(g_crid_config_mutex, portMAX_DELAY);
                }
                g_crid_config.init_lat = t_lat;
                g_crid_config.init_lon = t_lon;
                g_crid_config.flight_mode = (uint8_t)t_mode;
                config_snapshot = g_crid_config;
                if (g_crid_config_mutex != NULL) {
                    xSemaphoreGive(g_crid_config_mutex);
                }

                printf("\r\n[CLI SUCCESS] Target updated to Lat:%lf, Lon:%lf, Mode:%d\n", t_lat, t_lon, t_mode);

                // 持久化存储，下次掉电不丢失
                crid_nvs_save_config(&config_snapshot);
            } else {
                printf("\r\n[CLI ERROR] Unknown command or invalid mode.\n");
            }
        }
    }
    free(data);
}

void crid_cli_init(void) {
    // 确保开发板串口驱动在核心层已初始化完毕
    xTaskCreate(cli_task, "crid_cli_task", 4096, NULL, 5, NULL);
}