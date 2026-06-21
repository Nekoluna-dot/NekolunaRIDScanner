#!/usr/bin/env python3
"""
中国 C-RID 信号探测器 (修正版)
符合《民用微轻小型无人驾驶航空器运行识别最低性能要求（试行）》标准
"""

import sys
import struct
import time
from datetime import datetime
from collections import defaultdict
from scapy.all import *
from scapy.layers.dot11 import Dot11, Dot11Elt

class ChinaCRIDReceiver:
    def __init__(self):
        # C-RID 常量 (GB42590-2023)
        self.CRID_OUI = b'\xFA\x0B\xBC'
        self.CRID_VENDOR_TYPE = 0x0D
        
        # 消息类型映射
        self.msg_type_names = {
            0: "Basic ID",
            1: "Location/Vector", 
            2: "Authentication",
            3: "Self ID",
            4: "System",
            5: "Operator ID",
            0xF: "Packed Message"
        }
        
        # ID 类型映射 (符合试行标准)
        self.id_type_names = {
            0: "None/Undeclared",
            1: "Serial Number", 
            2: "CAA Registration ID",  # 中国标准要求
            3: "UTM Assigned UUID", 
            4: "Specific Session ID"
        }
        
        # UA 类型映射 (符合试行标准)
        self.ua_type_names = {
            0: "None/Not declared",
            1: "Aeroplane/Fixed wing", 
            2: "Helicopter/Multirotor",  # 最常见类型
            3: "Gyroplane",
            4: "Hybrid Lift",
            5: "Ornithopter",
            6: "Glider",
            7: "Kite",
            8: "Free Balloon",
            9: "Captive Balloon", 
            10: "Airship",
            11: "Free Fall/Parachute",
            12: "Rocket",
            13: "Tethered Powered Aircraft",
            14: "Ground Obstacle",
            15: "Other"
        }
        
        # 状态类型映射
        self.status_names = {
            0: "Undeclared", 
            1: "Ground", 
            2: "Airborne",  # 中国标准要求
            3: "Emergency", 
            4: "Remote ID System Failure"
        }
        
        # 分类类型映射
        self.classification_names = {
            0: "Undeclared", 
            1: "EU", 
            2: "Other"
        }
        
        # EU 类别映射
        self.eu_category_names = {
            0: "Undeclared", 1: "Class 0", 2: "Class 1", 3: "Class 2", 
            4: "Class 3", 5: "Class 4", 6: "Class 5", 7: "Class 6"
        }
        
        # EU 级别映射
        self.eu_class_names = {
            0: "Undeclared", 1: "Class I", 2: "Class II", 3: "Class III", 
            4: "Class IV", 5: "Class V", 6: "Class VI", 7: "Class VII"
        }
        
        # 高度参考类型映射
        self.height_ref_names = {
            0: "Over Takeoff",
            1: "Over Ground"
        }
        
        # 精度映射
        self.horiz_accuracy_names = [
            "Unknown", "<= 1m", "<= 2m", "<= 3m", "<= 4m", "<= 6m", 
            "<= 10m", "<= 15m", "<= 20m", "<= 25m", "<= 30m", "<= 35m", 
            "<= 40m", "<= 45m", "<= 50m", "N/A"
        ]
        
        self.vert_accuracy_names = [
            "Unknown", "<= 1m", "<= 2m", "<= 3m", "<= 4m", "<= 5m", 
            "<= 6m", "<= 7m", "<= 8m", "<= 9m", "<= 10m", "<= 15m", 
            "<= 20m", "<= 25m", "<= 30m", "N/A"
        ]
        
        self.speed_accuracy_names = [
            "Unknown", "<= 0.1m/s", "<= 0.2m/s", "<= 0.3m/s", "<= 0.4m/s", "<= 0.5m/s", 
            "<= 0.6m/s", "<= 0.7m/s", "<= 0.8m/s", "<= 0.9m/s", "<= 1.0m/s", 
            "<= 1.5m/s", "<= 2.0m/s", "<= 2.5m/s", "<= 3.0m/s", "N/A"
        ]
        
        # 统计信息
        self.stats = defaultdict(int)
        self.last_update = time.time()
        self.known_drones = {}
        self.frame_counter = 0

    def parse_basic_id_message(self, data_bytes):
        """解析中国 C-RID Basic ID 消息 (符合试行标准表3)"""
        if len(data_bytes) < 25:
            return None
            
        # 确保输入是字节类型
        if isinstance(data_bytes, list):
            data = bytes(data_bytes)
        else:
            data = data_bytes
            
        # 第1字节: [消息类型(高4位)] + [接口版本(低4位)] = 0x01 (Basic ID + Version 1)
        msg_type = (data[0] >> 4) & 0x0F
        interface_version = data[0] & 0x0F
        
        # 检查是否是 Basic ID 消息
        if msg_type != 0:
            return None
            
        # 第2字节: [ID类型(高4位)] + [UA类型(低4位)] - 符合试行标准表3
        id_ua_byte = data[1]
        id_type = (id_ua_byte >> 4) & 0x0F  # 高4位
        ua_type = id_ua_byte & 0x0F         # 低4位
        
        # 第3-22字节: UAS ID (20字节, ASCII字符, 不足填充空格)
        uas_id_bytes = data[2:22]  # 修正：从字节2开始，长度20
        uas_id = uas_id_bytes.rstrip(b'\x00 \x20').decode('ascii', errors='ignore')
        
        # 第23-25字节: 预留
        reserved = data[22:25]
        
        return {
            'message_type': 'Basic ID',
            'interface_version': interface_version,
            'id_type': self.id_type_names.get(id_type, f"Unknown ({id_type})"),
            'ua_type': self.ua_type_names.get(ua_type, f"Unknown ({ua_type})"),
            'uas_id': uas_id,
            'id_type_raw': id_type,
            'ua_type_raw': ua_type,
            'china_compliant': id_type == 2,  # CAA Registration ID (中国标准要求)
            'reserved_bytes': reserved
        }

    def parse_location_message(self, data_bytes):
        """解析中国 C-RID Location 消息 (符合试行标准表4)"""
        if len(data_bytes) < 39:
            return None
            
        # 确保输入是字节类型
        if isinstance(data_bytes, list):
            data = bytes(data_bytes)
        else:
            data = data_bytes
            
        # 第1字节: [消息类型(高4位)] + [接口版本(低4位)] = 0x11 (Location + Version 1)
        msg_type = (data[0] >> 4) & 0x0F
        interface_version = data[0] & 0x0F
        
        # 检查是否是 Location 消息
        if msg_type != 1:
            return None
            
        # 第2字节: [状态(高4位)] + [标志位组合(低4位)] - 符合试行标准表4
        flags_byte = data[1]
        status = (flags_byte >> 4) & 0x0F
        flags_low = flags_byte & 0x0F
        
        # 解析航向 (第3-4字节) - 符合试行标准表4
        direction_scaled = struct.unpack('<H', data[2:4])[0]  # Little endian
        direction = direction_scaled / 10.0  # 0.1度单位
        
        # 水平速度 (第5字节) - 符合试行标准表4
        speed_h = data[4] / 10.0  # 0.1m/s单位
        
        # 垂直速度 (第6字节, signed) - 符合试行标准表4
        speed_v = struct.unpack('<b', data[5:6])[0] / 10.0  # 0.1m/s单位
        
        # 纬度 (第7-14字节, 1E-7度单位, little endian) - 符合试行标准表4
        lat = struct.unpack('<d', data[6:14])[0]
        
        # 经度 (第15-22字节, 1E-7度单位, little endian) - 符合试行标准表4
        lon = struct.unpack('<d', data[14:22])[0]
        
        # 气压高度 (第23-26字节, cm, little endian) - 符合试行标准表4
        alt_baro = struct.unpack('<f', data[22:26])[0] / 100.0  # 转换为米
        
        # 地理高度 (第27-30字节, cm, little endian) - 符合试行标准表4
        alt_geo = struct.unpack('<f', data[26:30])[0] / 100.0  # 转换为米
        
        # 相对地面高度 (第31-34字节, cm, little endian) - 符合试行标准表4
        height = struct.unpack('<f', data[30:34])[0] / 100.0  # 转换为米
        
        # 高度类型 (第35字节) - 符合试行标准表4
        height_type = data[34]
        
        # 精度信息 (第36-39字节) - 符合试行标准表4
        horiz_accuracy = data[35]
        vert_accuracy = data[36]
        speed_accuracy = data[38]
        
        return {
            'message_type': 'Location/Vector',
            'interface_version': interface_version,
            'status': self.status_names.get(status, f"Unknown ({status})"),
            'status_raw': status,
            'direction': direction,
            'speed_horizontal': speed_h,
            'speed_vertical': speed_v,
            'latitude': lat,
            'longitude': lon,
            'altitude_baro': alt_baro,
            'altitude_geo': alt_geo,
            'height': height,
            'height_type': self.height_ref_names.get(height_type, f"Unknown ({height_type})"),
            'horiz_accuracy': self.horiz_accuracy_names[horiz_accuracy] if horiz_accuracy <= 15 else "Invalid",
            'vert_accuracy': self.vert_accuracy_names[vert_accuracy] if vert_accuracy <= 15 else "Invalid",
            'speed_accuracy': self.speed_accuracy_names[speed_accuracy] if speed_accuracy <= 15 else "Invalid",
            'accurate_enough': (horiz_accuracy <= 4 and vert_accuracy <= 4),  # 中国精度要求
            'flags': flags_low
        }

    def parse_system_message(self, data_bytes):
        """解析中国 C-RID System 消息 (符合试行标准表6)"""
        if len(data_bytes) < 39:
            return None
            
        # 确保输入是字节类型
        if isinstance(data_bytes, list):
            data = bytes(data_bytes)
        else:
            data = data_bytes
            
        # 第1字节: [消息类型(高4位)] + [接口版本(低4位)] = 0x41 (System + Version 1)
        msg_type = (data[0] >> 4) & 0x0F
        interface_version = data[0] & 0x0F
        
        # 检查是否是 System 消息
        if msg_type != 4:
            return None
            
        # 第2字节: [坐标系类型(高1位)] + [等级分类归属区域(中3位)] + [控制站位置类型(低2位)] - 符合试行标准表6
        sys_flags = data[1]
        coordinate_system = (sys_flags >> 7) & 0x01
        classification_region = (sys_flags >> 4) & 0x07
        operator_location_type = sys_flags & 0x03
        
        # 第3-10字节: 控制站纬度 (1E-7度单位, little endian) - 符合试行标准表6
        operator_lat = struct.unpack('<d', data[2:10])[0]
        
        # 第11-18字节: 控制站经度 (1E-7度单位, little endian) - 符合试行标准表6
        operator_lon = struct.unpack('<d', data[10:18])[0]
        
        # 第19-20字节: 运行区域计数 (little endian) - 符合试行标准表6
        area_count = struct.unpack('<H', data[18:20])[0]
        
        # 第21字节: 运行区域半径 - 符合试行标准表6
        area_radius = data[20] * 10  # 半径值 * 10
        
        # 第22-25字节: 运行区域高度上限 (little endian, cm) - 符合试行标准表6
        area_ceiling = struct.unpack('<f', data[21:25])[0] / 100.0  # 转换为米
        
        # 第26-29字节: 运行区域高度下限 (little endian, cm) - 符合试行标准表6
        area_floor = struct.unpack('<f', data[25:29])[0] / 100.0  # 转换为米
        
        # 第30字节: [EU类别(高4位)] + [EU级别(低4位)] - 符合试行标准表6
        category_class_byte = data[29]
        category_eu = (category_class_byte >> 4) & 0x0F
        class_eu = category_class_byte & 0x0F
        
        # 第31-34字节: 操作员地理高度 (little endian, cm) - 符合试行标准表6
        operator_alt = struct.unpack('<f', data[30:34])[0] / 100.0  # 转换为米
        
        # 第35-38字节: 时间戳 (little endian, seconds since 2019-01-01) - 符合试行标准表6
        timestamp = struct.unpack('<I', data[34:38])[0]
        
        return {
            'message_type': 'System',
            'interface_version': interface_version,
            'coordinate_system': coordinate_system,
            'classification_region': classification_region,
            'operator_location_type': operator_location_type,
            'operator_latitude': operator_lat,
            'operator_longitude': operator_lon,
            'area_count': area_count,
            'area_radius': area_radius,
            'area_ceiling': area_ceiling,
            'area_floor': area_floor,
            'category_eu': self.eu_category_names.get(category_eu, f"Unknown ({category_eu})"),
            'class_eu': self.eu_class_names.get(class_eu, f"Unknown ({class_eu})"),
            'operator_altitude': operator_alt,
            'timestamp': timestamp,
            'china_compliant': classification_region == 2  # 中国区域代码
        }

    def find_crid_in_frame(self, raw_bytes):
        """在原始帧中查找中国 C-RID 消息"""
        # 查找 GB42590 OUI (FA 0B BC)
        for i in range(len(raw_bytes) - 10):
            if (raw_bytes[i:i+3] == self.CRID_OUI and 
                i + 4 < len(raw_bytes) and 
                raw_bytes[i+3] == self.CRID_VENDOR_TYPE):
                
                oui_pos = i
                msg_counter = raw_bytes[oui_pos + 4]  # 消息计数器
                
                # 从消息计数器后面开始解析
                offset = oui_pos + 5
                messages = []
                
                # 尝试解析打包消息格式 (符合试行标准 3.1.5)
                if offset + 2 < len(raw_bytes):
                    packed_msg_len = raw_bytes[offset]  # 每个消息长度 (应该为0x19=25)
                    msg_count = raw_bytes[offset + 1]   # 消息数量
                    offset += 2
                    
                    if packed_msg_len == 0x19 and msg_count > 0:  # 25字节格式
                        # 解析打包的消息
                        for msg_idx in range(min(msg_count, 10)):  # 最多解析10条消息
                            if offset + 25 <= len(raw_bytes):
                                msg_data = raw_bytes[offset:offset + 25]
                                parsed_msg = self.parse_opendroneid_message(msg_data)
                                if parsed_msg:
                                    parsed_msg['counter'] = msg_counter
                                    messages.append(parsed_msg)
                                offset += 25
                            else:
                                break
                    else:
                        # 解析单个消息
                        while offset < len(raw_bytes) - 2:
                            if offset >= len(raw_bytes):
                                break
                                
                            sub_msg_type = raw_bytes[offset]
                            offset += 1
                            
                            # 根据消息类型解析相应长度的数据
                            if sub_msg_type == 0x00:  # Basic ID
                                if offset + 24 <= len(raw_bytes):
                                    basic_data = [sub_msg_type] + list(raw_bytes[offset:offset + 24])
                                    parsed_msg = self.parse_basic_id_message(basic_data)
                                    if parsed_msg:
                                        parsed_msg['counter'] = msg_counter
                                        messages.append(parsed_msg)
                                    offset += 24
                            elif sub_msg_type == 0x01:  # Location
                                if offset + 38 <= len(raw_bytes):
                                    location_data = [sub_msg_type] + list(raw_bytes[offset:offset + 38])
                                    parsed_msg = self.parse_location_message(location_data)
                                    if parsed_msg:
                                        parsed_msg['counter'] = msg_counter
                                        messages.append(parsed_msg)
                                    offset += 38
                            elif sub_msg_type == 0x04:  # System
                                if offset + 38 <= len(raw_bytes):
                                    system_data = [sub_msg_type] + list(raw_bytes[offset:offset + 38])
                                    parsed_msg = self.parse_system_message(system_data)
                                    if parsed_msg:
                                        parsed_msg['counter'] = msg_counter
                                        messages.append(parsed_msg)
                                    offset += 38
                            else:
                                # 跳过未知消息类型
                                break
                
                return messages if messages else None
        
        return None

    def parse_opendroneid_message(self, msg_data):
        """解析 OpenDroneID 消息"""
        if len(msg_data) == 0:
            return None
            
        if isinstance(msg_data, list):
            data = bytes(msg_data)
        else:
            data = msg_data
            
        if len(data) == 0:
            return None
            
        msg_type = (data[0] >> 4) & 0x0F  # 高4位是消息类型
        
        if msg_type == 0:  # Basic ID
            return self.parse_basic_id_message(data)
        elif msg_type == 1:  # Location
            return self.parse_location_message(data)
        elif msg_type == 4:  # System
            return self.parse_system_message(data)
        else:
            return {
                'message_type': f'Unknown Type {msg_type}',
                'raw_data': data.hex()
            }

    def update_drone_info(self, mac, messages):
        """更新无人机信息"""
        if mac not in self.known_drones:
            self.known_drones[mac] = {
                'first_seen': datetime.now(),
                'last_seen': datetime.now(),
                'messages': {}
            }
        
        self.known_drones[mac]['last_seen'] = datetime.now()
        
        for msg in messages:
            self.known_drones[mac]['messages'][msg['message_type']] = msg

    def print_detailed_crid_data(self, messages, source_mac):
        """打印详细的 C-RID 数据"""
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
        
        print(f"\n{'='*120}")
        print(f"  🚁 中国无人机远程识别信号检测 [{timestamp}]")
        print(f"  📡 源 MAC: {source_mac}")
        print(f"  📋 GB42590-2023 + 试行标准 (25字节格式)")
        print(f"  🇨🇳 中国民用无人驾驶航空器系统安全要求")
        print(f"{'='*120}")
        
        has_china_compliant = False
        for msg in messages:
            if msg['message_type'] == 'Basic ID':
                print(f"  🆔 无人机身份信息 (符合试行标准表3):")
                print(f"    🆔 UAS ID: '{msg['uas_id']}'")
                print(f"    🏷️  ID 类型: {msg['id_type']} ({msg['id_type_raw']})")
                print(f"    🚁 机型: {msg['ua_type']} ({msg['ua_type_raw']})")
                
                is_china_compliant = msg['china_compliant']
                print(f"    🇨🇳 中国标准合规: {'✅ 是' if is_china_compliant else '❌ 否'}")
                if is_china_compliant:
                    has_china_compliant = True
            
            elif msg['message_type'] == 'Location/Vector':
                print(f"  📍 位置向量信息 (符合试行标准表4):")
                print(f"    🌍 纬度:  {msg['latitude']:.7f}°")
                print(f"    🌍 经度:  {msg['longitude']:.7f}°")
                print(f"    📏 高度:  {msg['altitude_baro']:.2f}m (气压), {msg['altitude_geo']:.2f}m (地理)")
                print(f"    📏 相对高度: {msg['height']:.2f}m")
                print(f"    🛬 飞行状态: {msg['status']} ({msg['status_raw']})")
                
                print(f"  ⚡ 速度信息:")
                print(f"    🧭 航向: {msg['direction']:.1f}°")
                print(f"    🚀 水平速度: {msg['speed_horizontal']:.2f} m/s")
                print(f"    🚀 垂直速度: {msg['speed_vertical']:.2f} m/s")
                
                print(f"  🎯 精度信息:")
                print(f"    🎯 水平精度: {msg['horiz_accuracy']}")
                print(f"    🎯 垂直精度: {msg['vert_accuracy']}")
                print(f"    🎯 速度精度: {msg['speed_accuracy']}")
                
                is_accurate = msg['accurate_enough']
                print(f"    🇨🇳 中国精度合规: {'✅ 是' if is_accurate else '❌ 否'}")
            
            elif msg['message_type'] == 'System':
                print(f"  🏭 系统信息 (符合试行标准表6):")
                print(f"    🧑 控制站位置类型: {msg['operator_location_type']}")
                print(f"    🧑 控制站位置: {msg['operator_latitude']:.7f}°, {msg['operator_longitude']:.7f}°")
                print(f"    🧑 控制站高度: {msg['operator_altitude']:.2f}m")
                print(f"    🌍 分类归属区域: {msg['classification_region']} (2=中国)")
                
                if msg['classification_region'] == 2:  # 中国区域
                    print(f"    🇨🇳 中国区域合规: ✅")
                
                print(f"    🗺️  区域信息: {msg['area_count']} 个区域, 半径 {msg['area_radius']}m")
                print(f"    🗺️  区域范围: {msg['area_floor']:.2f}m - {msg['area_ceiling']:.2f}m")
        
        print(f"  📦 消息计数器: {messages[0]['counter'] if messages else 'N/A'}")
        print(f"  📋 消息类型: {[msg['message_type'] for msg in messages]}")
        print(f"{'='*120}\n")

    def print_summary(self):
        """打印统计摘要"""
        now = time.time()
        if now - self.last_update >= 10:  # 每10秒打印一次
            print(f"\n📊 [统计] 总包: {self.stats['total_packets']}, "
                  f"管理包: {self.stats['management_packets']}, "
                  f"C-RID包: {self.stats['crid_packets']}, "
                  f"已知无人机: {len(self.known_drones)}")
            
            # 显示各消息类型统计
            msg_stats = {}
            china_compliant_count = 0
            for mac, drone_info in self.known_drones.items():
                for msg_type in drone_info['messages'].keys():
                    msg_stats[msg_type] = msg_stats.get(msg_type, 0) + 1
                
                # 检查 Basic ID 合规性
                basic_msg = drone_info['messages'].get('Basic ID')
                if basic_msg and basic_msg.get('china_compliant', False):
                    china_compliant_count += 1
            
            if msg_stats:
                print("  📦 消息类型分布:")
                for msg_type, count in msg_stats.items():
                    print(f"    {msg_type}: {count}")
            
            if len(self.known_drones) > 0:
                print(f"  🇨🇳 中国标准合规: {china_compliant_count}/{len(self.known_drones)} 台")
            
            self.last_update = now

    def packet_handler(self, packet):
        """处理单个 Wi-Fi 数据包"""
        self.stats['total_packets'] += 1
        
        if hasattr(packet, 'type') and packet.type == 0:  # Management frame
            self.stats['management_packets'] += 1
            
            src_mac = packet.addr2 if hasattr(packet, 'addr2') else 'Unknown'
            
            # 获取原始帧数据
            raw_bytes = bytes(packet)
            
            # 查找中国 C-RID 消息
            crid_messages = self.find_crid_in_frame(raw_bytes)
            
            if crid_messages:
                self.stats['crid_packets'] += 1
                
                # 更新无人机信息
                self.update_drone_info(src_mac, crid_messages)
                
                # 打印详细信息
                self.print_detailed_crid_data(crid_messages, src_mac)
        
        # 打印统计摘要
        self.print_summary()

def main():
    if len(sys.argv) < 2:
        print("用法: sudo python3 china_crid_receiver_fixed.py <interface>")
        print("示例: sudo python3 china_crid_receiver_fixed.py wlan1")
        print("\n确保接口设置为监控模式:")
        print("  sudo ip link set <interface> down")
        print("  sudo iw <interface> set monitor control")
        print("  sudo ip link set <interface> up")
        print("  sudo iw <interface> set channel 6")
        sys.exit(1)
    
    interface = sys.argv[1]
    print(f"🚀 中国 C-RID 信号探测器 (修正版)")
    print(f"📡 接口: {interface}")
    print(f"📋 检测 GB42590-2023 + 试行标准 C-RID 信号")
    print(f"🎯 正确解析 ID Type 和 UA Type (同一字节的高低位)")
    print(f"🔄 每10秒显示统计摘要")
    print(f"🛑 按 Ctrl+C 停止探测\n")
    
    receiver = ChinaCRIDReceiver()
    
    try:
        sniff(iface=interface, 
              prn=receiver.packet_handler, 
              store=0,
              filter="type mgt subtype beacon or type mgt subtype probe-req or type mgt subtype probe-resp")
    except KeyboardInterrupt:
        print(f"\n\n🛑 探测已停止")
        
        # 显示最终摘要
        if len(receiver.known_drones) > 0:
            print(f"\n{'='*100}")
            print(f"  🚁 最终无人机检测摘要")
            print(f"{'='*100}")
            
            china_compliant_count = 0
            for mac, drone_info in receiver.known_drones.items():
                basic_msg = drone_info['messages'].get('Basic ID')
                is_china_compliant = basic_msg and basic_msg.get('china_compliant', False)
                if is_china_compliant:
                    china_compliant_count += 1
                
                print(f"  MAC: {mac}")
                print(f"    首次检测: {drone_info['first_seen'].strftime('%H:%M:%S')}")
                print(f"    最后检测: {drone_info['last_seen'].strftime('%H:%M:%S')}")
                
                if basic_msg:
                    print(f"    UAS ID: {basic_msg['uas_id']}")
                    print(f"    ID类型: {basic_msg['id_type']} ({basic_msg['id_type_raw']})")
                    print(f"    机型: {basic_msg['ua_type']} ({basic_msg['ua_type_raw']})")
                    print(f"    中国标准: {'✅' if is_china_compliant else '❌'}")
                
                location_msg = drone_info['messages'].get('Location/Vector')
                if location_msg:
                    print(f"    位置: {location_msg['latitude']:.5f}, {location_msg['longitude']:.5f}")
                    print(f"    高度: {location_msg['altitude_baro']:.2f}m")
                    print(f"    速度: {location_msg['speed_horizontal']:.2f}m/s")
                    print(f"    精度: {location_msg['horiz_accuracy']}, {location_msg['vert_accuracy']}")
                
                print(f"    消息类型: {list(drone_info['messages'].keys())}")
                print()
            
            print(f"  🇨🇳 中国标准合规: {china_compliant_count}/{len(receiver.known_drones)} 台")
        
        print(f"\n📊 最终统计:")
        print(f"  📦 总包数: {receiver.stats['total_packets']}")
        print(f"  📦 管理包: {receiver.stats['management_packets']}")
        print(f"  🚁 C-RID包: {receiver.stats['crid_packets']}")
        print(f"  🚁 已知无人机: {len(receiver.known_drones)}")

if __name__ == "__main__":
    main()