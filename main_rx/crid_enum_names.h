/**
 * crid_enum_names.h — 共享枚举→名称映射表 (X-Macro 模式)
 *
 * 设计目的：
 *   - 消除 crid_display.c 与 crid_json.c 中重复的枚举→字符串映射
 *   - 每行格式: X(枚举值, 显示名称, JSON名称)
 *   - 使用 ENUM_DISPLAY_FN / ENUM_JSON_FN 宏生成查找函数
 *   - 编译为 switch 跳转表，零额外内存开销
 */

#ifndef CRID_ENUM_NAMES_H
#define CRID_ENUM_NAMES_H

#include <stdint.h>
#include "opendroneid.h"
#include "crid_rx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * X-Macro 枚举映射表
 * 格式: X(枚举值, 显示名称, JSON名称)
 * ================================================================ */

/* --- ID Type --- */
#define ID_TYPE_MAP(X) \
    X(ODID_IDTYPE_NONE,                "None",                "none") \
    X(ODID_IDTYPE_SERIAL_NUMBER,       "Serial Number",       "serial_number") \
    X(ODID_IDTYPE_CAA_REGISTRATION_ID, "CAA Registration ID", "caa_registration_id") \
    X(ODID_IDTYPE_UTM_ASSIGNED_UUID,   "UTM Assigned UUID",   "utm_assigned_uuid") \
    X(ODID_IDTYPE_SPECIFIC_SESSION_ID, "Specific Session ID", "specific_session_id")

/* --- UA Type --- */
#define UA_TYPE_MAP(X) \
    X(ODID_UATYPE_NONE,                     "None",                    "none") \
    X(ODID_UATYPE_AEROPLANE,                "Aeroplane",               "aeroplane") \
    X(ODID_UATYPE_HELICOPTER_OR_MULTIROTOR, "Helicopter/Multirotor",   "helicopter_or_multirotor") \
    X(ODID_UATYPE_GYROPLANE,                "Gyroplane",               "gyroplane") \
    X(ODID_UATYPE_HYBRID_LIFT,              "Hybrid Lift",             "hybrid_lift") \
    X(ODID_UATYPE_ORNITHOPTER,              "Ornithopter",             "ornithopter") \
    X(ODID_UATYPE_GLIDER,                   "Glider",                  "glider") \
    X(ODID_UATYPE_KITE,                     "Kite",                    "kite") \
    X(ODID_UATYPE_FREE_BALLOON,             "Free Balloon",            "free_balloon") \
    X(ODID_UATYPE_CAPTIVE_BALLOON,          "Captive Balloon",         "captive_balloon") \
    X(ODID_UATYPE_AIRSHIP,                  "Airship",                 "airship") \
    X(ODID_UATYPE_FREE_FALL_PARACHUTE,      "Free Fall/Parachute",     "free_fall_parachute") \
    X(ODID_UATYPE_ROCKET,                   "Rocket",                  "rocket") \
    X(ODID_UATYPE_TETHERED_POWERED_AIRCRAFT,"Tethered Powered",        "tethered_powered") \
    X(ODID_UATYPE_GROUND_OBSTACLE,          "Ground Obstacle",         "ground_obstacle") \
    X(ODID_UATYPE_OTHER,                    "Other",                   "other")

/* --- Status --- */
#define STATUS_MAP(X) \
    X(ODID_STATUS_UNDECLARED,               "Undeclared",              "undeclared") \
    X(ODID_STATUS_GROUND,                   "Ground",                  "ground") \
    X(ODID_STATUS_AIRBORNE,                 "Airborne",                "airborne") \
    X(ODID_STATUS_EMERGENCY,                "Emergency",               "emergency") \
    X(ODID_STATUS_REMOTE_ID_SYSTEM_FAILURE, "RID System Failure",      "rid_system_failure")

/* --- Height Reference --- */
#define HEIGHT_REF_MAP(X) \
    X(ODID_HEIGHT_REF_OVER_TAKEOFF, "Over Takeoff", "over_takeoff") \
    X(ODID_HEIGHT_REF_OVER_GROUND,  "Over Ground",  "over_ground")

/* --- Description Type --- */
#define DESC_TYPE_MAP(X) \
    X(ODID_DESC_TYPE_TEXT,            "Text",            "text") \
    X(ODID_DESC_TYPE_EMERGENCY,       "Emergency",       "emergency") \
    X(ODID_DESC_TYPE_EXTENDED_STATUS, "Extended Status", "extended_status")

/* --- Operator Location Type --- */
#define OPERATOR_LOC_MAP(X) \
    X(ODID_OPERATOR_LOCATION_TYPE_TAKEOFF,   "Takeoff",   "takeoff") \
    X(ODID_OPERATOR_LOCATION_TYPE_LIVE_GNSS, "Live GNSS", "live_gnss") \
    X(ODID_OPERATOR_LOCATION_TYPE_FIXED,     "Fixed",     "fixed")

/* --- Auth Type --- */
#define AUTH_TYPE_MAP(X) \
    X(ODID_AUTH_NONE,                    "None",                    "none") \
    X(ODID_AUTH_UAS_ID_SIGNATURE,        "UAS ID Signature",        "uas_id_signature") \
    X(ODID_AUTH_OPERATOR_ID_SIGNATURE,   "Operator ID Signature",   "operator_id_signature") \
    X(ODID_AUTH_MESSAGE_SET_SIGNATURE,   "Message Set Signature",   "message_set_signature") \
    X(ODID_AUTH_NETWORK_REMOTE_ID,       "Network Remote ID",       "network_remote_id") \
    X(ODID_AUTH_SPECIFIC_AUTHENTICATION, "Specific Authentication", "specific_authentication")

/* ================================================================
 * 查找函数生成宏
 *
 * 用法:
 *   ENUM_DISPLAY_FN(get_id_type_name, ID_TYPE_MAP, "Unknown")
 *   ENUM_JSON_FN(s_id_type_name, ID_TYPE_MAP, "unknown")
 * ================================================================ */

#define ENUM_CASE_DISPLAY(v, d, j) case v: return d;
#define ENUM_CASE_JSON(v, d, j)    case v: return j;

#define ENUM_DISPLAY_FN(name, map, default_val) \
    static inline const char *name(uint8_t v) { \
        switch (v) { \
            map(ENUM_CASE_DISPLAY) \
            default: return default_val; \
        } \
    }

#define ENUM_JSON_FN(name, map, default_val) \
    static inline const char *name(uint8_t v) { \
        switch (v) { \
            map(ENUM_CASE_JSON) \
            default: return default_val; \
        } \
    }

#ifdef __cplusplus
}
#endif

#endif // CRID_ENUM_NAMES_H