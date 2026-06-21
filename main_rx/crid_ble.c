/**
 * crid_ble.c — BLE NUS 数据通道实现
 *
 * 使用 NimBLE 实现 Nordic UART Service (NUS) 外设，
 * 所有 JSON 数据通过 BLE 通知发送到已连接的客户端。
 *
 * 内存策略：
 *   发送缓冲区从 SPIRAM 分配，队列使用指针传递避免数据拷贝。
 */

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

/* ESP controller API */
#include "esp_bt.h"

/* NimBLE host stack */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

#include "crid_ble.h"

static const char *TAG = "RID_BLE";

/* ================================================================
 * NUS UUIDs (Nordic UART Service)
 * ================================================================ */

static const ble_uuid128_t gatt_svr_svc_nus_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);

static const ble_uuid128_t gatt_svr_chr_nus_tx_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);

static const ble_uuid128_t gatt_svr_chr_nus_rx_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

/* ================================================================
 * GATT 状态
 * ================================================================ */

static uint16_t g_nus_rx_handle;
static uint16_t g_nus_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool g_ble_initialized = false;
static esp_timer_handle_t g_adv_timer = NULL;
#define ADV_TIMEOUT_US (60 * 1000 * 1000)  /* 60秒 */

/* ================================================================
 * 数据发送队列 (指针传递，缓冲区从 SPIRAM 分配)
 * ================================================================ */

#define BLE_TX_QUEUE_LEN    16
#define BLE_TX_BUF_SIZE     512
#define BLE_TX_TASK_STACK   3072
#define BLE_TX_TASK_PRIO    3

static QueueHandle_t g_ble_tx_queue = NULL;

/* ================================================================
 * 前置声明
 * ================================================================ */

static int gatt_svr_svc_nus_access(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg);
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);
static void ble_host_task(void *param);
static void ble_tx_task(void *param);
static void ble_on_sync(void);
static void ble_advertise_start(void);

/* ================================================================
 * GATT 服务定义
 * ================================================================ */

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_nus_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &gatt_svr_chr_nus_tx_uuid.u,
                .access_cb = gatt_svr_svc_nus_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &gatt_svr_chr_nus_rx_uuid.u,
                .access_cb = gatt_svr_svc_nus_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    { 0 },
};

static int
gatt_svr_svc_nus_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    return 0;
}

/* 广播超时回调：60秒无人连接则关闭 BLE */
static void adv_timeout_cb(void *arg) {
    ESP_LOGI(TAG, "No BLE connection for 60s, stopping advertising");
    ble_gap_adv_stop();
    g_nus_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    if (g_adv_timer) {
        esp_timer_stop(g_adv_timer);
        esp_timer_delete(g_adv_timer);
        g_adv_timer = NULL;
    }
}

/* ================================================================
 * GAP 事件回调
 * ================================================================ */

static int
ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* 连接成功，取消广播超时定时器 */
        if (g_adv_timer) {
            esp_timer_stop(g_adv_timer);
            esp_timer_delete(g_adv_timer);
            g_adv_timer = NULL;
        }
        if (event->connect.status == 0) {
            g_nus_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected, conn_handle=%d", g_nus_conn_handle);
        } else {
            g_nus_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGE(TAG, "Connect failed (status=%d)", event->connect.status);
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGE(TAG, "Disconnected (reason=0x%04x)", event->disconnect.reason);
        g_nus_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ble_advertise_start();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_advertise_start();
        break;

    default:
        break;
    }

    return 0;
}

/* ================================================================
 * 广播配置
 * ================================================================ */

static void
ble_advertise_start(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name = "Nekoluna";

    memset(&fields, 0, sizeof(fields));
    memset(&adv_params, 0, sizeof(adv_params));

    /* 广播：设备名 + 标志（UUID 放扫描应答） */
    fields.name = (uint8_t *)name;
    fields.name_len = (uint8_t)strlen(name);
    fields.name_is_complete = 1;
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed (rc=%d)", rc);
        return;
    }

    /* 扫描应答：NUS 服务 UUID */
    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.uuids128 = (ble_uuid128_t *)&gatt_svr_svc_nus_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields failed (rc=%d)", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start failed (rc=%d)", rc);
        return;
    }

    ESP_LOGI(TAG, "Advertising as '%s' with NUS UUID", name);

    /* 启动 60 秒超时定时器：无人连接则自动关闭广播 */
    if (g_adv_timer) {
        esp_timer_stop(g_adv_timer);
        esp_timer_delete(g_adv_timer);
    }
    esp_timer_create_args_t tmr = { .callback = adv_timeout_cb, .arg = NULL, .name = "ble_adv_tmo" };
    if (esp_timer_create(&tmr, &g_adv_timer) == ESP_OK) {
        esp_timer_start_once(g_adv_timer, ADV_TIMEOUT_US);
    }
}

/* ================================================================
 * NimBLE 同步回调 — 主机就绪后开始广播并查找句柄
 * ================================================================ */

static void
ble_on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE host synced");

    /* 查找 RX characteristic 句柄 */
    int rc = ble_gatts_find_chr(&gatt_svr_svc_nus_uuid.u,
                                 &gatt_svr_chr_nus_rx_uuid.u,
                                 NULL, &g_nus_rx_handle);
    if (rc != 0) {
        ESP_LOGE(TAG, "RX characteristic not found (rc=%d)", rc);
        g_nus_rx_handle = 0;
    } else {
        ESP_LOGI(TAG, "RX handle=0x%04x", g_nus_rx_handle);
    }

    ble_advertise_start();
}

/* ================================================================
 * NimBLE 主机任务
 * ================================================================ */

static void
ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ================================================================
 * BLE 发送任务 — 从队列取数据并通过通知发送
 * ================================================================ */

static void
ble_tx_task(void *param)
{
    (void)param;
    char *buf;

    while (1) {
        if (xQueueReceive(g_ble_tx_queue, &buf, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!buf) continue;

        size_t data_len = strnlen(buf, BLE_TX_BUF_SIZE);
        if (data_len > 0 && g_nus_conn_handle != BLE_HS_CONN_HANDLE_NONE && g_nus_rx_handle != 0) {
            uint16_t mtu = ble_att_mtu(g_nus_conn_handle);
            uint16_t chunk_size = (mtu >= 6) ? (mtu - 3) : 20;
            size_t offset = 0;

            while (offset < data_len) {
                size_t send_len = data_len - offset;
                if (send_len > chunk_size) send_len = chunk_size;

                struct os_mbuf *om = ble_hs_mbuf_from_flat(buf + offset, send_len);
                if (om) {
                    int rc = ble_gattc_notify_custom(g_nus_conn_handle,
                                                      g_nus_rx_handle, om);
                    if (rc != 0) {
                        os_mbuf_free_chain(om);
                        break;
                    }
                }
                offset += send_len;
                if (offset < data_len) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }

        free(buf);
    }
}

/* ================================================================
 * 公开接口
 * ================================================================ */

esp_err_t
crid_ble_init(void)
{
    if (g_ble_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Initializing BLE (NimBLE NUS)...");

    /* 创建发送队列 (元素为 char* 指针) */
    g_ble_tx_queue = xQueueCreate(BLE_TX_QUEUE_LEN, sizeof(char *));
    if (!g_ble_tx_queue) {
        ESP_LOGE(TAG, "TX queue creation failed");
        return ESP_ERR_NO_MEM;
    }

    /* 设置同步回调（必须在 nimble_port_init 之前） */
    ble_hs_cfg.sync_cb = ble_on_sync;

    /* 初始化 NimBLE 控制器 + 主机 */
    nimble_port_init();

    /* 检查 BLE 控制器是否初始化成功 */
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
        ESP_LOGE(TAG, "BLE controller init failed, BLE disabled (C5 rev1.0 known issue)");
        vQueueDelete(g_ble_tx_queue);
        g_ble_tx_queue = NULL;
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* 注册 GATT 服务 */
    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT count failed (rc=%d)", rc);
        nimble_port_deinit();
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT add failed (rc=%d)", rc);
        nimble_port_deinit();
        return ESP_FAIL;
    }

    /* 启动 NimBLE 主机任务 */
    nimble_port_freertos_init(ble_host_task);

    /* 创建数据发送任务 */
    BaseType_t task_created = xTaskCreatePinnedToCore(ble_tx_task, "ble_tx",
                                BLE_TX_TASK_STACK, NULL, BLE_TX_TASK_PRIO, NULL,
                                tskNO_AFFINITY);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "BLE TX task creation failed");
        nimble_port_deinit();
        return ESP_ERR_NO_MEM;
    }

    g_ble_initialized = true;
    ESP_LOGI(TAG, "BLE initialized");
    return ESP_OK;
}

void
crid_ble_write_cb(const char *data, size_t len, void *ctx)
{
    (void)ctx;

    if (!g_ble_initialized || !g_ble_tx_queue) {
        return;
    }
    if (!data || len == 0) return;

    if (len > BLE_TX_BUF_SIZE - 1) {
        len = BLE_TX_BUF_SIZE - 1;
    }

    if (g_nus_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    char *buf = (char *)malloc(BLE_TX_BUF_SIZE);
    if (!buf) return;

    memcpy(buf, data, len);
    buf[len] = '\0';

    if (xQueueSend(g_ble_tx_queue, &buf, pdMS_TO_TICKS(10)) != pdTRUE) {
        free(buf);
    }
}

bool
crid_ble_is_connected(void)
{
    return g_nus_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}