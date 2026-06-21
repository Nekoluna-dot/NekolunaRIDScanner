# NekolunaRIDScanner v1.0.0

ESP32-C5 无人机 Remote ID 扫描器固件首次发布。

## 下载

| 文件 | 说明 |
| :--- | :--- |
| `NekolunaRIDScanner-v1.0.0-firmware.zip` | 扫描器固件（含 bootloader, partition-table, firmware） |
| `NekolunaRIDScanner-v1.0.0-simulator-firmware.zip` | 模拟器固件（可选，ESP32-C3） |
| `NekolunaRID-v1.0.0.apk` | Android 查看器（NekolunaRID） |

## 烧录方法

### 方法一：ESPFlash 图形化工具

1. 下载 [ESPFlash Download Tool](https://www.espressif.com/en/support/download/other-tools)
2. 解压 `NekolunaRIDScanner-v1.0.0-firmware.zip`
3. 打开 ESPFlash Download Tool，选择芯片 ESP32-C5
4. 按下表配置烧录地址：

| 地址 | 文件 |
| :--- | :--- |
| 0x0 | bootloader.bin |
| 0x8000 | partition-table.bin |
| 0x10000 | remoteid_scanner.bin |

5. 选择正确的 COM 口，波特率 115200，点击 START

### 方法二：命令行（esptool）
```bash
pip install esptool
esptool.py --chip esp32c5 -p COM7 -b 115200 write_flash \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 remoteid_scanner.bin
```

### 方法三：ESP-IDF 用户
```bash
idf.py set-target esp32c5 -DMAIN_DIR=main_rx
idf.py build
idf.py -p COM7 flash
```

### 模拟器（ESP32-C3）
```bash
esptool.py --chip esp32c3 -p COM6 -b 115200 write_flash \
  0x0 simulator-bootloader.bin \
  0x8000 simulator-partition-table.bin \
  0x10000 nekoluna-rid-sim.bin
```

## 首次使用

1. 给 ESP32-C5 上电，等待 LED 指示初始化完成
2. Android 手机安装 NekolunaRID.apk
3. 打开 App，通过 BLE 或 USB OTG 连接扫描器
4. 将扫描器放在室外或有无人机信号的区域
5. App 会自动显示探测到的无人机信息

## 更新内容

第一版发布。

- 支持 ASTM F3411-22a / GB 42590-2023 / GB 46750-2025 三种协议
- Wi-Fi 杂乱模式抓包，RSSI 阈值可配
- BLE NUS 输出，60 秒广告超时
- Android 端：飞机列表、天地图、轨迹、飞手定位、语音告警
- 内存经过大量优化，PSRAM 方案稳定运行
