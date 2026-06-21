import serial
import json
import time

# ESP32-S3 USB CDC 端口（Windows: COMx, Linux: /dev/ttyACM0）
# Linux
ser = serial.Serial('/dev/tty.usbserial-A5069RR4', 115200, timeout=1)
# Windows
# ser = serial.Serial('COM3', 115200, timeout=1)

print("Connected to ESP32-S3 ODID Scanner...")

while True:
    try:
        line = ser.readline().decode('utf-8').strip()
        if line.startswith('{'):
            data = json.loads(line)
            if data.get('event') == 'summary':
                print(f"\n📊 Summary: {data['active_count']} active UAVs detected")
                for uav in data['uavs']:
                    print(f"  MAC: {uav['mac']}, ID: {uav['uav_id']}, CH: {uav['channel']}, RSSI: {uav['rssi']}")
                    if 'latitude' in uav and uav['latitude'] != 0:
                        print(f"    Location: {uav['latitude']:.6f}, {uav['longitude']:.6f}")
            else:
                # 实时数据
                print(f"📡 ODID: {data['type']} | MAC: {data['mac']} | ID: {data['uav_id']} | CH: {data['channel']} | RSSI: {data['rssi']}")
                if data.get('latitude', 0) != 0:
                    print(f"    Location: {data['latitude']:.6f}, {data['longitude']:.6f}, Alt: {data['altitude_msl']:.1f}m")
    except json.JSONDecodeError:
        print(f"Invalid JSON: {line}")
    except Exception as e:
        print(f"Error: {e}")
        time.sleep(1)