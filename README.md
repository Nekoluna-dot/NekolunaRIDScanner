# NekolunaRIDScanner

[![GitHub release](https://img.shields.io/github/v/release/Nekoluna-dot/NekolunaRIDScanner?logo=github)](https://github.com/Nekoluna-dot/NekolunaRIDScanner/releases)
[![Stars](https://img.shields.io/github/stars/Nekoluna-dot/NekolunaRIDScanner?style=flat-square&logo=github)](https://github.com/Nekoluna-dot/NekolunaRIDScanner/stargazers)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-5.4+-blue?logo=espressif)](https://github.com/espressif/esp-idf)
[![License](https://img.shields.io/badge/license-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32--C5+Android-red)](#)

> **NekolunaRIDScanner：ねこるなは空を見守っている。**  
> ESP32-C5 无人机远程识别（Remote ID）扫描器 + Android 查看器(特定DPI，支持安卓手表!)。支持 ASTM F3411-22a、GB 42590-2023、GB 46750-2025 三种协议的解码与显示，配套 Android 端提供实时显示、地图追踪与异常告警。

---

## 目录

- [主要特性](#主要特性)
- [硬件配置](#硬件配置)
- [快速开始](#快速开始)
- [项目结构](#项目结构)
- [协议解析逻辑](#协议解析逻辑)
- [数据流](#数据流)
- [Android 端功能](#android-端功能)
- [技术问题与解决方案](#技术问题与解决方案)
- [构建指南](#构建指南)
- [配置说明](#配置说明)
- [FAQ](#faq)
- [许可证](#许可证)

---

## 主要特性

| 功能 | 描述 |
| :--- | :--- |
| **多协议** | ASTM F3411-22a / GB 42590-2023 / GB 46750-2025 自动识别与解码 |
| **Wi-Fi 嗅探** | 802.11 混杂模式抓取 Beacon 帧，OUI FA:0B:BC，Channel 6 固定 |
| **BLE 透传** | Nordic UART Service (NUS)，设备名 Nekoluna，60 秒广告超时 |
| **多路输出** | USB CDC / UART1 / BLE Notify 三路 JSON 数据扇出 |
| **Android 查看器** | 飞机列表、实时数据流、飞手/用户定位 |
| **异常告警** | 新飞机发现时红色闪烁 + 震动 + 语音播报 |
| **天地图集成** | Tianditu vec_w + cva_w 双图层，中国境内可用的地图瓦片 |
| **主题系统** | 三种配色主题 + 自定义背景图片 + 常亮模式 |
| **RSSI 过滤** | 支持动态 RSSI 阈值配置（默认 -110 dBm 不过滤） |
| **支持安卓手表** | 我使用OPPO Watch3 42mm显示，功能都正常(仅BLE) |

---

## 快速开始

### 硬件准备
1. 一块 ESP32-C5 开发板（建议带 PSRAM）
2. 一根 USB-C 数据线
3. （可选）Android 手机用于接收数据

### 烧录扫描器
```bash
idf.py set-target esp32c5 -DMAIN_DIR=main_rx
idf.py build
idf.py -p COM7 flash
```

或用 esptool 直接烧录：
```bash
pip install esptool
esptool.py --chip esp32c5 -p COM7 -b 115200 write_flash \
  0x2000 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 remoteid_scanner.bin
```

### 烧录模拟器
```bash
cd esp32-crid-sim-OTA
idf.py set-target esp32c3
idf.py build
idf.py -p COM6 flash
```

或用 esptool：
```bash
esptool.py --chip esp32c3 -p COM6 -b 115200 write_flash \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 esp32-crid-sim.bin
```

### Android 端
APK 下载地址见 [Releases](https://github.com/Nekoluna-dot/NekolunaRIDScanner/releases) 页面。

> Android 端源码不开源，仅提供编译好的 APK。

## 使用教程

### 1. 硬件连接
```
ESP32-C5                 Android 手机
┌──────────┐    BLE     ┌──────────┐
│          │◄──────────►│          │
│ Nekoluna │    or      │RID Viewer│
│ Scanner  │◄──USB OTG──┤          │
│          │            └──────────┘
└──────────┘
```
- 扫描器通过 USB-C 供电（5V），上电后自动开始扫描
- BLE 方式：手机直接蓝牙连接扫描器，设备名 "Nekoluna"
- USB 方式：手机通过 OTG 线连接扫描器（需支持 USB 串口功能）

### 2. 启动流程
1. 给 ESP32-C5 插入 USB 电源，红色 LED 亮起表示供电正常
2. 等待约 5 秒，系统完成初始化，开始扫描 Wi-Fi Beacon 帧
3. 打开 Android App，在"设备"标签页切换连接模式（USB / BLE）
4. BLE 模式：点击"扫描 BLE"，找到 "Nekoluna" 设备，点击"连接"
5. USB 模式：插上 OTG 线，点击"请求 USB 权限"，允许后自动连接
6. 状态栏显示 "已连接" 即表示成功

### 3. 查看无人机
- 连接成功后，附近有无人机发射 RID 信号时会自动显示在"飞机"列表
- 每架无人机显示：MAC 地址、信号强度 RSSI、高度、速度、位置
- 点击任意无人机可展开查看详细信息（序列号、类型、操作员 ID 等）
- 长按无人机可选择导航到该飞机或飞手位置

### 4. 地图功能
- 切换到"地图"标签页，已探测到的无人机会显示在地图上
- **红色标记**：无人机当前位置
- **红色折线**：无人机飞行轨迹
- **绿色圆点**：飞手位置（需在设置中配置）
- **蓝色圆点**：你的位置（自动获取 GPS）
- 首次发现无人机会自动缩放至该区域

### 5. 告警设置
- 发现新无人机时 App 会全屏红色闪烁 + 震动
- 可在设置中选择语音播报（派蒙 / 纳西妲 / 荧）
- 所有告警均可单独开关


**调试开发**
```
扫描器连电脑 USB
→ idf.py monitor 看串口日志
→ Python 工具 esp32monitor.py 实时展示
```

---

## 项目结构

```
NekolunaRIDScanner/
├── main_rx/                      # 扫描器固件（ESP32-C5）
│   ├── app_main.c                主入口：初始化、任务创建、数据扇出
│   ├── crid_sniffer.c            802.11 混杂模式抓包
│   ├── crid_parser_common.c      协议分发：GB 46750 / GB 42590 / ASTM
│   ├── crid_parser_astm.c        ASTM F3411 Packed 解码
│   ├── crid_parser_gb42590.c     GB 42590-2023 Packed 解码
│   ├── crid_parser_gb46750.c     GB 46750-2025 标识位解析
│   ├── crid_tracker.c            MAC 追踪表，30 秒超时
│   ├── crid_json.c               JSON 输出（UART1 + BLE + stdout）
│   ├── crid_display.c            终端展示
│   ├── crid_ble.c                NimBLE NUS 外设
│   ├── crid_usb_net.c            USB NCM 网络接口
│   ├── crid_rx_types.h           配置常量
│   └── crid_enum_names.h         枚举映射
├── esp32-crid-sim-OTA/           # 模拟器固件（ESP32-C3）
│   └── main/
│       ├── main.c                1Hz Beacon 发射
│       ├── encode_gb42590.c      信标帧编码
│       ├── encode_astm.c         ASTM 信标帧编码
│       ├── crid_patrol.c         越秀公园巡游路径模拟
│       ├── rid_wifi.c            SoftAP + 原始帧注入
│       └── rid_config.c          NVS 配置持久化
├── components/opendroneid/       OpenDroneID 解码库
├── tools/                        Python 工具
│   ├── esp32monitor.py           TUI 监视器
│   └── china_crid_receiver_fixed.py  GB 兼容接收器
├── partition_table/
│   └── partitionTable.csv
├── CMakeLists.txt                顶层 CMake
└── sdkconfig                     构建配置（本地）
```

---

## 协议解析逻辑

三种协议共用 OUI FA:0B:BC 和 Vendor Type 0x0D，解析器通过 payload 首字节 Magic 区分：

```
data[0] == 0xFF -> GB 46750-2025
data[0] == 0xF1 -> GB 42590-2023
data[0] == 0xF2 -> ASTM F3411 Packed
data[0] in 0x00-0x0F -> ASTM F3411 单消息（Fallback）
```

### GB 46750-2025
首字节 0xFF，3 字节 flag 位映射 21 个数据项，按位顺序排列 payload。解码后执行健康检查：unique_id 必须包含可打印 ASCII 字符，否则重置并尝试下一协议。

### GB 42590-2023
首字节 0xF1，固定 25 字节/消息，替换头部后调 `odid_message_process_pack` 解码。

### ASTM F3411-22a
首字节 0xF2 或 0x00-0x0F，调 `odid_message_process_pack` 或 `decodeOpenDroneID` 解码。

---

## 数据流

### 扫描器
```
Wi-Fi Beacon
  -> crid_sniffer ISR (xQueueSendFromISR)
    -> parser_task (xQueueReceive)
      -> crid_parser_decode (协议自动识别)
      -> crid_tracker_find_or_create (MAC 索引)
      -> crid_json
        -> data_write_fanout (stdout / UART1 / BLE NUS Notify)
```

### Android 端
```
BLE NUS Notify / USB 串口
  -> MainActivity.onData (行拆分 + 粘包处理)
    -> tryParseJson (JSON 解析)
      -> findOrCreate (MAC 去重)
        -> 更新列表 + 地图标记 + 轨迹
```

### 事件类型
| 事件 | 触发时机 |
| :--- | :--- |
| startup | 系统启动 |
| status | 每 60 秒 |
| uav_discovery | 新 MAC 首次解码成功 |
| uav_update | 每次解码后 |
| uav_timeout | 30 秒无更新 |
| no_aircraft | 每秒无活跃 UAV |
| warning/error/debug | 异常 |

状态栏格式：`USB|0.0p/s|2/5在线`

---

## Android 端功能

| 功能 | 说明 |
| :--- | :--- |
| **飞机列表** | 显示全部探测到的无人机，展开查看详细信息，长按可导航到飞机或飞手 |
| **数据流** | 实时 JSON 数据流显示（原始报文） |
| **地图** | 天地图（vec_w + cva_w），无人机红色标记 + 轨迹折线，飞手绿色圆点，用户蓝色圆点，首次发现自动缩放 |
| **设置** | 三种主题（默认深色/绿色/紫蓝）、背景图片、常亮、地图开关、报警设置（闪烁/震动/MP3 语音） |
| **连接模式** | USB 串口 / BLE NUS 一键切换 |
| **BLE 扫描** | 手动扫描 BLE 设备，选择连接 |
| **数据保存** | 导出 JSON 到应用目录 |

---

## 技术问题与解决方案

### 1. BLE 控制器 DMA 与 PSRAM 冲突
**症状**：BLE 连接后 30 秒闪断，错误码 `BLE_ERR_CONN_TERM_LOCAL_RESOURCES` (0x0213)

ESP32-C5 rev1.0/v1.2 的 BLE 控制器使用 DMA 访问内存，PSRAM 通过 SPI 挂在不同的一致性域上。`CONFIG_SPIRAM_FETCH_INSTRUCTION` 和 `CONFIG_SPIRAM_RODATA` 让指令/只读数据跑到 PSRAM 里，DMA 读着读着就懵了。

**解决**：关闭 `FETCH_INSTRUCTION` 和 `RODATA`。NimBLE buffer pool 走 `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL` 就够了。

### 2. sdkconfig 里的 NimBLE 配置在打架
**症状**：设了 `EXTERNAL`，menuconfig 里却变成 `INTERNAL`

`CONFIG_NIMBLE_MEM_ALLOC_MODE_INTERNAL`（缺 `BT_`）和 `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL` 同时在 sdkconfig 里，互相覆盖。

**解决**：删掉 `CONFIG_NIMBLE_MEM_ALLOC_MODE_INTERNAL`。

### 3. OSMDroid TileSource 构造器签名不对
**症状**：`OnlineTileSourceBase` 构造时 NPE

OSMDroid 6.1.20 的构造器参数列表和旧版不同——没有 backup URL 数组，且需要显式传 `TileSourcePolicy`。

**解决**：`(name, minZoom, maxZoom, tileSize, extension, baseUrl, copyright, tileSourcePolicy)` 8 参数版。

### 4. Marker(new MapView(null)) 之谜
**症状**：判了 `mapView == null` 还 NPE

`recreate()` 时 Activity 重建，定时器回调和对象销毁之间的竞态问题。

**解决**：局部变量快照 + 多重判空 + try-catch。

### 5. 天地图没有地名
**症状**：地图能显示但无文字标注

Tianditu 的 `vec_w` 是纯底图，地名在 `cva_w` 注记层。

**解决**：叠加 `TilesOverlay`，`vec_w` + `cva_w` 双图层。

### 6. MapView 和 ViewPager2 抢触摸
**症状**：滑动地图时触发翻页

**解决**：`OnTouchListener` 调 `requestDisallowInterceptTouchEvent(true)`。

### 7. NUS 通知方向不对
**症状**：BLE 连上了但收不到数据

Android 监听了 TX 特征值（6E400002），ESP32 发在 RX 特征值（6E400003）。

**解决**：Android 改监听 RX UUID。

### 8. Polyline.setPoints() NPE
**症状**：新 Polyline 调 `setPoints()` 崩溃

OSMDroid 无参构造器未初始化内部 `LinearRing`。

**解决**：try-catch 包裹。

---

## 构建指南

### 环境要求
- [ESP-IDF](https://github.com/espressif/esp-idf) v5.4 或更新版本
- Python 3.10+
- Git

### 扫描器
```bash
idf.py set-target esp32c5 -DMAIN_DIR=main_rx
idf.py build
idf.py -p COM7 flash
idf.py -p COM7 monitor
```

### 模拟器
```bash
cd esp32-crid-sim-OTA
idf.py set-target esp32c3
idf.py build
idf.py -p COM6 flash
```

### Android
APK 从 [Releases](https://github.com/Nekoluna-dot/NekolunaRIDScanner/releases) 下载安装。

---

## 配置说明

通过 `idf.py menuconfig` 修改。关键配置项：

| 配置路径 | 推荐值 |
| :--- | :--- |
| Bluetooth -> NimBLE -> Mode | Peripheral + Broadcaster |
| Bluetooth -> NimBLE -> Memory Alloc | External (PSRAM) |
| Component config -> ESP PSRAM | SPI RAM, USE_CAPS_ALLOC |
| NimBLE -> ACL Buffer Count | 16 |
| NimBLE -> MSYS1 | 16 |
| NimBLE -> MSYS2 | 8 |
| Partition Table | Single factory app (large) |
| Wi-Fi RX Buffer | 20 |
| Wi-Fi Dynamic RX | 16 |
| Wi-Fi Dynamic TX | 8 |
| RSSI 阈值 | -110 dBm（不过滤） |

---

## FAQ

**Q：必须用 ESP32-C5 吗？**
A：理论上任何 ESP32 系列都可以，但代码在 C5 上测试通过。C3/S3 需要调整 sdkconfig 中的 BLE 和 PSRAM 配置。

**Q：能用手机直接看无人机数据吗？**
A：可以。Android 端通过 BLE 或 USB OTG 连接扫描器，实时显示。

**Q：为什么地图上没有地名？**
A：确保 Android 端使用最新版本，已修复天地图 cva_w 注记叠加问题。如果还不行，检查网络环境是否能访问 t0.tianditu.gov.cn。

**Q：Android 端会开源吗？**
A：目前不开源，仅提供 APK 下载。

喵~ 祝你使用愉快~
---

## 许可证

本项目基于 MIT 许可证开源。`components/opendroneid/` 目录下的代码遵循其原有许可证。

```
MIT License

Copyright (c) 2026 Nekoluna-dot

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
