/**
 * crid_rx_types.h — 接收端公共类型定义与配置常量
 *
 * ESP32 Remote ID Scanner
 * Standards: ASTM F3411-22a / ASD-STAN prEN 4709-002 / GB 42590-2023 / GB 46750-2025
 */

#ifndef CRID_RX_TYPES_H
#define CRID_RX_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "opendroneid.h"

/* ================================================================
 * 程序版本号 (开发阶段以编译时间为主)
 * ================================================================ */
#define CRID_VERSION_MAJOR     0
#define CRID_VERSION_MINOR     1
#define CRID_VERSION_PATCH     0


/* ================================================================
 * 协议标准 OUI 定义
 *
 * Wi-Fi Beacon 统一使用 FA:0B:BC（ASTM F3411-22a / ASD-STAN / GB 42590-2023 / GB 46750-2025）
 * ASTM 与国标在 Wire 格式上相同（OUI FA:0B:BC, Vendor Type 0x0D, Packed 消息格式），
 * 无法从数据字节直接区分协议类型。
 *
 * OUI 来源：
 *   RID_TRANSPORT_WIFI_BEACON_OUI  = 0xFA0BBC  (Wi-Fi Beacon)
 *   RID_TRANSPORT_WIFI_NAN_OUI     = 0x506F9A  (Wi-Fi NAN)
 *   RID_TRANSPORT_BLUETOOTH_OUI    = 0xFFFA    (Bluetooth Legacy/LR)
 *   厂商扩展：DJI 专用 OUI
 */

// ---- Wi-Fi Beacon OUI (0xFA0BBC) ----
#define OUI_BEACON_0  0xFA
#define OUI_BEACON_1  0x0B
#define OUI_BEACON_2  0xBC

// ---- Wi-Fi NAN OUI (0x506F9A) ----
#define OUI_NAN_0  0x50
#define OUI_NAN_1  0x6F
#define OUI_NAN_2  0x9A

// ---- Bluetooth OUI (0xFFFA) ----
// BLE Legacy Advertising / Long Range
#define OUI_BLE_0  0xFF
#define OUI_BLE_1  0xFF
#define OUI_BLE_2  0xFA


// ---- 厂商扩展 OUI ----
// DJI 厂商
#define OUI_DJI_60_0  0x60
#define OUI_DJI_60_1  0x60
#define OUI_DJI_60_2  0x1F
#define OUI_DJI_48_0  0x48
#define OUI_DJI_48_1  0x1C
#define OUI_DJI_48_2  0xB9
#define OUI_DJI_34_0  0x34
#define OUI_DJI_34_1  0xD2
#define OUI_DJI_34_2  0x62


// 判断是否为任一 Remote ID OUI
// 注：Wi-Fi Beacon 标准 OUI 统一为 FA:0B:BC（ASTM / ASD-STAN / GB 42590 均使用此 OUI）
#define IS_RID_OUI(o0, o1, o2) \
    (((o0) == OUI_BEACON_0   && (o1) == OUI_BEACON_1   && (o2) == OUI_BEACON_2)   || \
     ((o0) == OUI_NAN_0      && (o1) == OUI_NAN_1      && (o2) == OUI_NAN_2)      || \
     ((o0) == OUI_BLE_0      && (o1) == OUI_BLE_1      && (o2) == OUI_BLE_2)      || \
     ((o0) == OUI_DJI_60_0   && (o1) == OUI_DJI_60_1   && (o2) == OUI_DJI_60_2)   || \
     ((o0) == OUI_DJI_48_0   && (o1) == OUI_DJI_48_1   && (o2) == OUI_DJI_48_2)   || \
     ((o0) == OUI_DJI_34_0   && (o1) == OUI_DJI_34_1   && (o2) == OUI_DJI_34_2)) 

// 根据 OUI 获取传输类型
#define GET_RID_TRANSPORT(o0, o1, o2) \
    (((o0) == OUI_BEACON_0 && (o1) == OUI_BEACON_1 && (o2) == OUI_BEACON_2) ? RID_TRANSPORT_WIFI_BEACON : \
     ((o0) == OUI_NAN_0    && (o1) == OUI_NAN_1    && (o2) == OUI_NAN_2)    ? RID_TRANSPORT_WIFI_NAN    : \
     ((o0) == OUI_BLE_0    && (o1) == OUI_BLE_1    && (o2) == OUI_BLE_2)    ? RID_TRANSPORT_BLUETOOTH_LEGACY : \
     RID_TRANSPORT_WIFI_BEACON)  // 默认按 Beacon 处理

/* ================================================================
 * 传输方式定义 (基于 ASTM F3411-22a Section 5.4)
 * ================================================================ */

typedef enum {
    RID_TRANSPORT_BLUETOOTH_LEGACY     = 0,
    RID_TRANSPORT_BLUETOOTH_LONG_RANGE = 1,
    RID_TRANSPORT_WIFI_NAN             = 2,
    RID_TRANSPORT_WIFI_BEACON          = 3,
} rid_transport_t;

// 传输方式最大 payload 大小
#define RID_PAYLOAD_BLE_LEGACY_MAX     25
#define RID_PAYLOAD_BLE_LONG_RANGE_MAX 255
#define RID_PAYLOAD_WIFI_NAN_MAX       255
#define RID_PAYLOAD_WIFI_BEACON_MAX    250

// Wi-Fi Beacon Vendor Type (ASD-STAN)
#define RID_WIFI_BEACON_VENDOR_TYPE    0x0D

/* ================================================================
 * 协议类型枚举 (参照 ORIP types.h)
 * ================================================================ */

typedef enum {
    RID_PROTOCOL_UNKNOWN    = 0,
    RID_PROTOCOL_ASTM_F3411 = 1,
    RID_PROTOCOL_ASD_STAN   = 2,
    RID_PROTOCOL_GB42590    = 3,   // 中国 GB 42590-2023
    RID_PROTOCOL_GB46750    = 4,   // 中国 GB 46750-2025
} rid_protocol_t;

/* ================================================================
 * 分层数据结构 (参照 ORIP types.h)
 *
 * 设计思想：
 *   - 与底层 opendroneid 库的 ODID_UAS_Data 解耦
 *   - 每个结构体有独立的 valid 标志
 *   - 便于未来扩展 GB 42590 专有字段
 *   - 显示层直接读取这些结构体，无需理解 ODID_* 枚举
 * ================================================================ */

// 位置/矢量数据
typedef struct {
    bool     valid;
    double   latitude;           // 纬度 (-90° ~ 90°)
    double   longitude;          // 经度 (-180° ~ 180°)
    float    altitude_baro;      // 气压高度 (m)
    float    altitude_geo;       // 大地高度 (m, WGS84)
    float    height;             // 相对高度 (m)
    uint8_t  height_ref;         // 0=Takeoff, 1=Ground (ODID_Height_reference_t)
    float    speed_horizontal;   // 水平速度 (m/s)
    float    speed_vertical;     // 垂直速度 (m/s)
    float    direction;          // 航向 (0°~360°)
    uint8_t  status;             // UAV 状态 (ODID_status_t)
    uint8_t  h_accuracy;         // 水平精度 (ODID_Horizontal_accuracy_t)
    uint8_t  v_accuracy;         // 垂直精度 (ODID_Vertical_accuracy_t)
    uint8_t  baro_accuracy;      // 气压精度 (ODID_Vertical_accuracy_t)
    uint8_t  speed_accuracy;     // 速度精度 (ODID_Speed_accuracy_t)
    uint8_t  ts_accuracy;        // 时间戳精度 (ODID_Timestamp_accuracy_t)
    float    timestamp;          // 相对于整小时的秒数 (0.1s 单位)
} rid_location_t;

// 系统信息（操作员/飞行员数据）
typedef struct {
    bool     valid;
    uint8_t  operator_location_type;  // 操作员位置类型 (ODID_operator_location_type_t)
    double   operator_latitude;       // 操作员纬度
    double   operator_longitude;      // 操作员经度
    float    operator_altitude_geo;   // 操作员大地高度 (m)
    uint16_t area_count;              // 区域内飞行器数量
    uint16_t area_radius;             // 区域半径 (m)
    float    area_ceiling;            // 运行区域上限 (m)
    float    area_floor;              // 运行区域下限 (m)
    uint8_t  classification_type;     // 分类类型 (ODID_classification_type_t)
    uint8_t  category_eu;             // EU 类别 (ODID_category_EU_t)
    uint8_t  class_eu;                // EU 等级 (ODID_class_EU_t)
    uint32_t timestamp;               // Unix 时间戳
} rid_system_info_t;

// Self-ID 消息
typedef struct {
    bool     valid;
    uint8_t  description_type;        // 描述类型 (ODID_desctype_t)
    char     description[24];         // 自由格式文本 (最多 23 字符 + null)
} rid_self_id_t;

// 操作员 ID 消息
typedef struct {
    bool     valid;
    uint8_t  id_type;                 // ID 类型 (ODID_operatorIdType_t)
    char     id[21];                  // 操作员 ID (最多 20 字符 + null)
} rid_operator_id_t;

// Basic ID 消息
typedef struct {
    bool     valid;
    uint8_t  id_type;                 // ID 类型 (ODID_idtype_t)
    uint8_t  ua_type;                 // UA 类型 (ODID_uatype_t)
    char     uas_id[21];              // UAS ID (最多 20 字符 + null)
} rid_basic_id_t;

/* ================================================================
 * GB 46750-2025 专用数据结构
 *
 * 数据标识位表定义了 21 个数据内容项（001-021），
 * 每个标识位为 1 表示对应数据项存在。
 *
 * 数据内容按标识位顺序排列在标识字节之后，
 * 每项的长度由数据类型决定。
 * ================================================================ */

// GB 46750 解析后的无人机数据
typedef struct {
    bool     valid;                   // 是否已解析

    // --- 标识字节1 ---
    bool     has_unique_id;           // 001 唯一产品识别码 (M)，固定20字节 ASCII，大端序
    char     unique_id[21];           // 唯一产品识别码字符串 (20 + null)

    bool     has_realname_flag;       // 002 实名登记标志 (M)，固定8字节 ASCII，大端序
    char     realname_id[9];          // 实名登记号码后8位 (8 + null)

    bool     has_operation_category;  // 003 运行类别 (O)
    uint8_t  operation_category;      // 运行类别枚举

    bool     has_ua_category;         // 004 民用无人驾驶航空器分类 (M)
    uint8_t  ua_category;             // 分类枚举

    bool     has_rcs_loc_type;        // 005 遥控站位置类型 (M)
    uint8_t  rcs_loc_type;            // 位置类型枚举

    bool     has_rcs_location;        // 006 遥控站位置 (M)
    float    rcs_latitude;            // 遥控站纬度
    float    rcs_longitude;           // 遥控站经度

    bool     has_rcs_altitude;        // 007 遥控站高度 (M)
    float    rcs_altitude;            // 遥控站大地高度 (m)

    // --- 标识字节2 ---
    bool     has_uav_location;        // 008 民用无人驾驶航空器位置 (M)
    float    uav_latitude;            // 无人机纬度
    float    uav_longitude;           // 无人机经度

    bool     has_track_angle;         // 009 航迹角 (M)
    float    track_angle;             // 航迹角 (0°~360°)

    bool     has_ground_speed;        // 010 地速 (M)
    float    ground_speed;            // 地速 (m/s)

    bool     has_relative_height;     // 011 相对高度 (O)
    float    relative_height;         // 相对高度 (m)

    bool     has_vertical_speed;      // 012 垂直速度 (O)
    float    vertical_speed;          // 垂直速度 (m/s, 正=上升)

    bool     has_geo_altitude;        // 013 大地高度 (M)
    float    geo_altitude;            // 大地高度 WGS84 (m)

    bool     has_baro_altitude;       // 014 气压高度 (O)
    float    baro_altitude;           // 气压高度 (m)

    // --- 标识字节3 ---
    bool     has_operation_status;    // 015 运行状态 (M)
    uint8_t  operation_status;        // 运行状态枚举

    bool     has_coord_system;        // 016 坐标系类型 (M)
    uint8_t  coord_system;            // 坐标系类型枚举

    bool     has_h_accuracy;          // 017 水平精度 (M)
    uint8_t  h_accuracy;              // 水平精度枚举

    bool     has_v_accuracy;          // 018 垂直精度 (M)
    uint8_t  v_accuracy;              // 垂直精度枚举

    bool     has_speed_accuracy;      // 019 速度精度 (M)
    uint8_t  speed_accuracy;          // 速度精度枚举

    bool     has_timestamp;           // 020 时间戳 (M)，6字节小端序，Unix毫秒时间戳
    uint64_t timestamp_ms;            // Unix 毫秒时间戳

    bool     has_ts_accuracy;         // 021 时间戳精度 (M)
    uint8_t  ts_accuracy;             // 时间戳精度枚举

    // 扩展标志位
    bool     has_ext_byte1;           // 标识字节1有扩展
    bool     has_ext_byte2;           // 标识字节2有扩展
    bool     has_ext_byte3;           // 标识字节3有扩展
} gb46750_data_t;

/* ================================================================
 * 扫描配置常量
 * ================================================================ */

#define MAX_TRACKED_UAVS        30         // 最多同时追踪的无人机数量
#define SNIFFER_QUEUE_SIZE      32          // sniffer 消息队列深度
#define PARSER_TASK_STACK       8192        // 解析任务栈大小
#define MONITOR_TASK_STACK      4096        // 监控任务栈大小
#define PARSER_TASK_PRIO        4           // 解析任务优先级
#define MONITOR_TASK_PRIO       3           // 监控任务优先级
#define CH_HOLD_TASK_STACK      3072        // 信道保持任务栈大小
#define CH_HOLD_TASK_PRIO       2           // 信道保持任务优先级

// 锁定监听信道
#define FIXED_CHANNEL           6

// 最小 RSSI 阈值 (dBm)，低于此值的信号被忽略
#define RID_MIN_RSSI_DEFAULT    -110
extern int8_t g_rid_min_rssi;

// 无人机超时时间 (毫秒)
#define UAV_TIMEOUT_MS          30000       // 30 秒

/* ================================================================
 * 802.11 MAC 头结构 (packed)
 * ================================================================ */

typedef struct __attribute__((__packed__)) {
    uint16_t frame_ctrl;
    uint16_t duration_id;
    uint8_t  addr1[6];
    uint8_t  addr2[6];
    uint8_t  addr3[6];
    uint16_t seq_ctrl;
} wifi_mac_hdr_t;

/* ================================================================
 * Sniffer 消息队列元素
 * ================================================================ */

// 消息类型枚举
typedef enum {
    MSG_TYPE_RID = 0,           // RID Vendor IE
    MSG_TYPE_NON_RID_VENDOR,    // 非 RID Vendor IE (ID=221, 其他 OUI)
    MSG_TYPE_BEACON_NO_VENDOR,  // 普通 Beacon（不含 Vendor IE 或诊断采样）
} sniffer_msg_type_t;

typedef struct {
    uint8_t  src_mac[6];        // 源 MAC 地址
    int8_t   rssi;              // 信号强度
    uint8_t  channel;           // 接收信道
    uint32_t timestamp_ms;      // 接收时间戳
    uint8_t  oui_type;          // OUI 类型字节
    uint16_t data_len;          // 有效数据长度
    uint8_t  data[256];         // Vendor Specific IE 数据 (最大255)
    bool     is_rid;            // 是否为 RID 消息
    uint8_t  oui[3];            // 完整 OUI 字节 (用于非 RID 诊断)
    sniffer_msg_type_t msg_type; // 消息类型
    uint8_t  ssid_len;          // SSID 长度 (0-32)
    uint8_t  ssid[32];          // SSID (用于 Beacon 诊断)
    bool     has_vendor_ie;     // 是否包含 Vendor IE (ID=221)
} sniffer_msg_t;

/* ================================================================
 * 单个无人机追踪状态
 * ================================================================ */

typedef struct {
    uint8_t  mac[6];                    // 源 MAC 地址
    bool     active;                    // 是否活跃
    uint32_t last_seen_ms;              // 最后收到信号的时间
    int8_t   last_rssi;                 // 最近 RSSI
    uint8_t  last_channel;              // 最近所在信道
    uint8_t  oui[3];                    // Remote ID OUI 字节
    uint8_t  oui_type;                  // OUI Type 字节
    uint8_t  transport;                 // 传输方式 (rid_transport_t)
    uint8_t  protocol;                  // 协议类型 (rid_protocol_t)

    // 使用 opendroneid 库的标准数据结构 (底层解码目标)
    ODID_UAS_Data uas_data;             // 完整无人机数据
    ODID_MessagePack_data last_pack;    // 最近接收的消息包

    // 分层数据视图 (从 uas_data 提取，供显示层直接使用)
    rid_basic_id_t    basic_id;         // Basic ID
    rid_location_t    location;         // 位置/速度
    rid_system_info_t system;           // 系统/操作员信息
    rid_self_id_t     self_id;          // Self-ID 描述
    rid_operator_id_t operator_id;      // 操作员 ID

    // GB 46750-2025 专用数据
    gb46750_data_t    gb46750;          // GB 46750 解析数据

    // 统计
    uint32_t msg_count;                 // 累计消息数
    uint32_t first_seen_ms;             // 首次发现时间
} uav_track_t;

/* ================================================================
 * 全局统计
 * ================================================================ */

typedef struct {
    volatile uint32_t total_packets;
    volatile uint32_t mgmt_frames;
    volatile uint32_t rid_detections;
    volatile uint32_t queue_overflows;
    volatile uint32_t non_rid_vendor_ie;   // 非 RID OUI 的 Vendor IE 计数
    volatile uint32_t beacon_count;         // Beacon 帧总数
} sniffer_stats_t;

#endif // CRID_RX_TYPES_H
