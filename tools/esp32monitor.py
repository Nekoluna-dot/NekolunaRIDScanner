#!/usr/bin/env python3
"""
ESP32 Monitor — TUI 实时监视 ESP32 Remote ID Scanner 的 JSON 输出

用法:
    python3 esp32monitor.py [--port PORT] [--baud BAUD] [--mode MODE]

模式:
    data    - 仅显示 UAV 数据（默认）
    debug   - 仅显示调试信息
    static  - 仅显示无人机静态消息
    all     - 显示所有事件

快捷键:
    q / Ctrl+C  退出
    m           切换显示模式
    r           刷新/清屏
    s           显示会话摘要
"""

import argparse
import json
import os
import sys
import time
import threading
from collections import OrderedDict
from datetime import datetime, timezone
from queue import Queue, Empty

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("需要 pyserial: pip install pyserial")
    sys.exit(1)

try:
    from rich.live import Live
    from rich.table import Table
    from rich.panel import Panel
    from rich.layout import Layout
    from rich.text import Text
    from rich.console import Console
    from rich import box
    from rich.align import Align
    from rich.columns import Columns
except ImportError:
    print("需要 rich: pip install rich")
    sys.exit(1)


# ── 数据模型 ──────────────────────────────────────────────

class UAVState:
    """单个 UAV 的追踪状态"""
    __slots__ = ('mac', 'rssi', 'channel', 'msg_count', 'transport', 'protocol',
                 'basic_id', 'self_id', 'operator_id', 'system', 'auth',
                 'location', 'first_seen', 'last_seen', 'is_active')

    def __init__(self, mac=""):
        self.mac = mac
        self.rssi = "?"
        self.channel = "?"
        self.msg_count = 0
        self.transport = "?"
        self.protocol = "?"
        self.basic_id = None
        self.self_id = None
        self.operator_id = None
        self.system = None
        self.auth = None
        self.location = None
        self.first_seen = time.time()
        self.last_seen = time.time()
        self.is_active = True

    def update_from_json(self, obj):
        self.rssi = obj.get("rssi", self.rssi)
        self.channel = obj.get("channel", self.channel)
        self.msg_count = obj.get("msg_count", self.msg_count)
        self.transport = obj.get("transport", self.transport)
        self.protocol = obj.get("protocol", self.protocol)
        if obj.get("basic_id"):
            self.basic_id = obj["basic_id"]
        if obj.get("self_id"):
            self.self_id = obj["self_id"]
        if obj.get("operator_id"):
            self.operator_id = obj["operator_id"]
        if obj.get("system"):
            self.system = obj["system"]
        if obj.get("auth"):
            self.auth = obj["auth"]
        if obj.get("location"):
            self.location = obj["location"]
        self.last_seen = time.time()
        self.is_active = True


class AppState:
    """全局应用状态"""
    def __init__(self):
        self.uavs = OrderedDict()  # mac -> UAVState
        self.start_time = time.time()
        self.mode = "data"
        self.running = True
        self.status = {}           # 最新 status 事件
        self.log_lines = []        # 最近日志行 (最多 200)
        self.startup_info = {}
        self.total_pkts = 0
        self.decode_fails = 0
        self.discovery_count = 0
        self.timeout_count = 0
        self.warning_count = 0
        self.error_count = 0
        self.lock = threading.Lock()

    def get_mode_desc(self):
        return {"data": "UAV 数据", "debug": "调试信息", "static": "静态消息", "all": "全部事件"}.get(self.mode, self.mode)

    def get_run_duration(self):
        secs = time.time() - self.start_time
        if secs < 60:
            return f"{secs:.0f}s"
        elif secs < 3600:
            return f"{int(secs)//60}m {int(secs)%60}s"
        else:
            h = int(secs) // 3600
            m = (int(secs) % 3600) // 60
            return f"{h}h {m}m"

    def get_active_count(self):
        now = time.time()
        return sum(1 for u in self.uavs.values() if now - u.last_seen < 300)


# ── 全局状态 ──────────────────────────────────────────────

state = AppState()
event_queue = Queue()
console = Console()


# ── 数据端口 / 调试端口事件分类 ───────────────────────────

DATA_EVENTS = {"uav_discovery", "uav_update", "uav_status", "uav_timeout", "uav_detail", "status"}
DEBUG_EVENTS = {"startup", "warning", "error", "debug", "decode_fail"}


# ── 事件处理 ──────────────────────────────────────────────

def event_visible(evt):
    """判断事件在当前模式下是否可见"""
    mode = state.mode
    if mode == "all":
        return True
    if mode == "data":
        return evt in DATA_EVENTS
    if mode == "debug":
        return evt in DEBUG_EVENTS
    if mode == "static":
        return evt in ("uav_update", "uav_discovery", "uav_timeout", "status", "warning", "error")
    return True


def process_event(obj):
    """处理单个 JSON 事件，更新 state"""
    evt = obj.get("evt", "unknown")
    ts_ms = obj.get("ts", 0)
    ts_str = format_ts(ts_ms)

    with state.lock:
        if evt == "startup":
            state.startup_info = obj
            state.log_lines.append(f"[cyan]{ts_str}[/] [bold cyan]🚀 启动[/] {escape_markup(obj.get('name','?'))} v{escape_markup(obj.get('version','?'))}")

        elif evt == "status":
            state.status = obj
            loop = obj.get("loop_min", "?")
            active = obj.get("active_uavs", 0)
            total = obj.get("total_pkts", 0)
            rate = obj.get("pkts_per_sec", 0)
            state.total_pkts = total
            state.log_lines.append(f"[cyan]{ts_str}[/] [blue]📊 状态[/] 运行{loop}min 活跃UAV:{active} 总包:{total}({rate:.1f}/s)")

        elif evt in ("uav_discovery", "uav_update", "uav_detail"):
            mac = obj.get("mac", "")
            if mac:
                is_new = mac not in state.uavs
                if is_new:
                    state.uavs[mac] = UAVState(mac)
                    state.uavs[mac].first_seen = time.time()
                state.uavs[mac].update_from_json(obj)
                state.uavs.move_to_end(mac)
                if is_new:
                    state.discovery_count += 1
                    state.log_lines.append(f"[cyan]{ts_str}[/] [green]🆕 发现[/] {escape_markup(mac)}")

        elif evt == "uav_timeout":
            mac = obj.get("mac", "")
            if mac and mac in state.uavs:
                state.uavs[mac].is_active = False
                state.timeout_count += 1
                state.log_lines.append(f"[cyan]{ts_str}[/] [yellow]⏰ 超时[/] {escape_markup(mac)}")

        elif evt == "uav_status":
            mac = obj.get("mac", "?")
            age = obj.get("age_ms", 0)
            rssi = obj.get("rssi", "?")
            state.log_lines.append(f"[cyan]{ts_str}[/] [dim]📡 {escape_markup(mac)} age={age/1000:.0f}s RSSI={rssi}[/]")

        elif evt == "decode_fail":
            state.decode_fails += 1
            byte0 = obj.get("byte0", 0)
            byte1 = obj.get("byte1", 0)
            if isinstance(byte0, str):
                state.log_lines.append(f"[cyan]{ts_str}[/] [red]❓ 解码失败[/] byte0={escape_markup(byte0)} byte1={escape_markup(byte1)}")
            else:
                state.log_lines.append(f"[cyan]{ts_str}[/] [red]❓ 解码失败[/] byte0=0x{byte0:02X} byte1=0x{byte1:02X}")

        elif evt == "warning":
            state.warning_count += 1
            msg = escape_markup(obj.get("msg", ""))
            state.log_lines.append(f"[cyan]{ts_str}[/] [yellow]⚠ {escape_markup(obj.get('module','?'))}[/] {msg}")

        elif evt == "error":
            state.error_count += 1
            msg = escape_markup(obj.get("msg", ""))
            state.log_lines.append(f"[cyan]{ts_str}[/] [red]❌ {escape_markup(obj.get('module','?'))}[/] {msg}")

        elif evt == "debug":
            msg = escape_markup(obj.get("msg", ""))
            state.log_lines.append(f"[cyan]{ts_str}[/] [dim]🔧 {escape_markup(obj.get('module','?'))}[/] {msg}")

        # 裁剪日志行
        if len(state.log_lines) > 200:
            state.log_lines = state.log_lines[-150:]


def escape_markup(text):
    """转义 rich markup 中的方括号，防止被误解析"""
    return str(text).replace("[", "\\[").replace("]", "\\]")

def format_ts(ms):
    """将毫秒时间戳转为 HH:MM:SS"""
    if ms == 0:
        return "--:--:--"
    s = ms / 1000.0
    return datetime.fromtimestamp(s, tz=timezone.utc).strftime("%H:%M:%S")


# ── TUI 渲染 ──────────────────────────────────────────────

def build_uav_table():
    """构建 UAV 列表表格"""
    table = Table(box=box.SIMPLE_HEAVY, expand=True, show_header=True,
                  header_style="bold cyan", border_style="blue")
    table.add_column("MAC", style="cyan", width=19)
    table.add_column("RSSI", style="yellow", width=6)
    table.add_column("ch", width=3)
    table.add_column("msgs", width=5, justify="right")
    table.add_column("传输", width=10)
    table.add_column("协议", width=10)
    table.add_column("UAS ID / 描述", style="green", width=22)
    table.add_column("位置", style="bright_white", width=26)
    table.add_column("alt/hdg/spd", width=16)
    table.add_column("seen", width=6, justify="right")

    now = time.time()
    with state.lock:
        # 移除已超时超过 60 秒的 UAV
        stale = [mac for mac, u in state.uavs.items() if not u.is_active and now - u.last_seen > 60]
        for mac in stale:
            del state.uavs[mac]

        for mac, u in state.uavs.items():
            age = now - u.last_seen
            age_str = f"{age:.0f}s" if age < 120 else f"{age/60:.0f}m"
            active_style = "" if age < 30 else "dim"

            # 协议显示
            proto = u.protocol or "?"
            proto_style = {"GB 46750": "magenta", "GB 42590": "blue", "ASTM F3411": "cyan"}.get(proto, "white")

            # 传输方式显示
            transp = u.transport or "?"
            transp_style = {"Wi-Fi Beacon": "green", "Wi-Fi NAN": "bright_green",
                            "BT Legacy": "blue", "BT Long Range": "bright_blue"}.get(transp, "white")

            # UAS ID
            uas_id = ""
            if u.basic_id:
                uas_id = u.basic_id.get("uas_id", "") or ""
            if not uas_id and u.self_id:
                uas_id = u.self_id.get("desc", "") or ""
            if len(uas_id) > 22:
                uas_id = uas_id[:21] + "…"

            # 位置
            loc_str = ""
            alt_hdg_spd = ""
            if u.location:
                lat = u.location.get("latitude")
                lng = u.location.get("longitude")
                alt = u.location.get("alt_baro") or u.location.get("alt_geo")
                spd = u.location.get("speed_h")
                hdg = u.location.get("direction")

                if lat is not None and lng is not None:
                    loc_str = f"{lat:.5f}, {lng:.5f}"

                parts = []
                if alt is not None:
                    parts.append(f"{alt:.0f}m")
                if hdg is not None:
                    parts.append(f"→{hdg:.0f}°")
                if spd is not None:
                    parts.append(f"{spd:.1f}m/s")
                alt_hdg_spd = " ".join(parts)

            proto_text = Text(proto, style=proto_style)
            transp_text = Text(transp, style=transp_style)
            row_style = "dim" if active_style == "dim" else ""
            uas_text = Text(uas_id, style=row_style)
            loc_text = Text(loc_str, style=row_style)
            ahs_text = Text(alt_hdg_spd, style=row_style)
            age_text = Text(age_str, style=row_style)

            table.add_row(
                Text(mac, style="cyan"),
                str(u.rssi),
                str(u.channel),
                str(u.msg_count),
                transp_text,
                proto_text,
                uas_text,
                loc_text,
                ahs_text,
                age_text,
            )

    return table


def build_status_bar():
    """构建底部状态栏"""
    with state.lock:
        active = state.get_active_count()
        total = len(state.uavs)
        duration = state.get_run_duration()
        mode_desc = state.get_mode_desc()

        s = state.status
        total_pkts = s.get("total_pkts", state.total_pkts)
        pkt_rate = s.get("pkts_per_sec", 0)
        rid_rate = s.get("rid_per_sec", 0)
        beacons = s.get("beacons", 0)
        beacon_rate = s.get("beacons_per_sec", 0)

    left = Text()
    left.append("🛸 ", style="green")
    left.append(f"UAV: {active}活跃/{total}累计", style="bold green")
    left.append("  │  ")
    left.append(f"⏱ {duration}", style="cyan")
    left.append("  │  ")
    left.append(f"📦 包:{total_pkts} ({pkt_rate:.1f}/s)", style="yellow")
    left.append("  │  ")
    left.append(f"📡 RID:{rid_rate:.1f}/s", style="blue")
    if beacons:
        left.append(f"  📶 Beacon:{beacons}({beacon_rate:.1f}/s)")

    right = Text()
    right.append(f"模式: {mode_desc} ", style="bold magenta")
    right.append("│ ")
    right.append("m=切换模式 ", style="dim")
    right.append("q=退出 ", style="dim")
    right.append("s=摘要", style="dim")

    return Align(Columns([left, Align(right, align="right")]), align="left")


def build_log_panel():
    """构建日志面板"""
    with state.lock:
        lines = state.log_lines[-12:]
    if not lines:
        return Panel(Text("等待数据...", style="dim"), title="日志", border_style="blue")
    text = Text.from_markup("\n".join(lines))
    return Panel(text, title="日志", border_style="blue", height=14)


def build_header():
    """构建顶部标题栏"""
    with state.lock:
        si = state.startup_info
        name = si.get("name", "ESP32 Remote ID Scanner") if si else "ESP32 Remote ID Scanner"
        ver = si.get("version", "?") if si else "?"
        target = si.get("target", "?") if si else "?"

    title = Text()
    title.append("🛰  ", style="bold cyan")
    title.append(f"{escape_markup(name)} v{escape_markup(ver)}", style="bold white")
    title.append(f"  [{escape_markup(target)}]", style="dim cyan")
    return Panel(title, border_style="cyan", padding=(0, 2))


def render_layout():
    """构建完整布局（data / all 模式）"""
    layout = Layout()
    layout.split(
        Layout(name="header", size=3),
        Layout(name="main", ratio=1),
        Layout(name="status_bar", size=1),
    )
    layout["main"].split_column(
        Layout(name="uav_table", ratio=2),
        Layout(name="log", size=14),
    )
    layout["header"].update(build_header())
    layout["uav_table"].update(build_uav_table())
    layout["log"].update(build_log_panel())
    layout["status_bar"].update(build_status_bar())
    return layout


def build_static_view():
    """静态消息模式视图"""
    with state.lock:
        lines = []
        for mac, u in state.uavs.items():
            lines.append(f"[cyan]{escape_markup(mac)}[/]  [yellow]RSSI={u.rssi}[/]  ch={u.channel}  {escape_markup(str(u.transport))}/{escape_markup(str(u.protocol))}")
            if u.basic_id:
                bid = u.basic_id
                lines.append(f"  🆔 type={escape_markup(bid.get('id_type','?'))} ua_type={escape_markup(bid.get('ua_type','?'))}")
                if bid.get("uas_id"):
                    lines.append(f"     UAS ID: [bold green]{escape_markup(bid['uas_id'])}[/]")
            if u.self_id and u.self_id.get("desc"):
                lines.append(f"  📝 Self ID: [{escape_markup(u.self_id.get('type','?'))}] {escape_markup(u.self_id['desc'])}")
            if u.operator_id and u.operator_id.get("id"):
                lines.append(f"  👤 Operator: [{escape_markup(u.operator_id.get('type','?'))}] [bold]{escape_markup(u.operator_id['id'])}[/]")
            if u.system:
                s = u.system
                lat = s.get("operator_lat")
                lng = s.get("operator_lon")
                if lat is not None and lng is not None:
                    lines.append(f"  🧑 Operator pos: {lat:.7f}, {lng:.7f}")
                if s.get("area_count", 0) > 0:
                    lines.append(f"  📐 区域×{s['area_count']} r={s.get('area_radius',0)}m")
            if u.auth:
                lines.append(f"  🔐 Auth pages: {len(u.auth)}")
        if not lines:
            lines.append("[dim]等待 UAV 静态消息...[/]")

        log_tail = state.log_lines[-8:] if state.log_lines else ["[dim]等待数据...[/]"]

    layout = Layout()
    layout.split(
        Layout(name="header", size=3),
        Layout(name="main", ratio=1),
        Layout(name="status_bar", size=1),
    )
    layout["main"].split_column(
        Layout(name="static", ratio=2),
        Layout(name="log", size=12),
    )
    layout["header"].update(build_header())
    layout["static"].update(Panel(Text.from_markup("\n".join(lines)), title="静态消息", border_style="magenta"))
    layout["log"].update(Panel(Text.from_markup("\n".join(log_tail)), title="日志", border_style="blue", height=12))
    layout["status_bar"].update(build_status_bar())
    return layout


def build_debug_view():
    """调试模式视图"""
    with state.lock:
        log_tail = state.log_lines[-25:] if state.log_lines else ["[dim]等待调试输出...[/]"]
    layout = Layout()
    layout.split(
        Layout(name="header", size=3),
        Layout(name="log", ratio=1),
        Layout(name="status_bar", size=1),
    )
    layout["header"].update(build_header())
    layout["log"].update(Panel(Text.from_markup("\n".join(log_tail)), title="调试输出", border_style="yellow"))
    layout["status_bar"].update(build_status_bar())
    return layout


def build_summary():
    """构建摘要面板"""
    with state.lock:
        duration = state.get_run_duration()
        total = len(state.uavs)
        active = state.get_active_count()
        lines = []
        lines.append(f"[bold cyan]📊 会话摘要[/]")
        lines.append(f"  运行时长: [bold]{duration}[/]")
        lines.append(f"  累计发现: [green]{total}[/] 架 UAV  (当前活跃: [green]{active}[/])")
        lines.append(f"  发现事件: {state.discovery_count}  超时: {state.timeout_count}")
        if state.decode_fails or state.warning_count or state.error_count:
            parts = []
            if state.decode_fails:
                parts.append(f"❓ 解码失败: {state.decode_fails}")
            if state.warning_count:
                parts.append(f"⚠ 告警: {state.warning_count}")
            if state.error_count:
                parts.append(f"❌ 错误: {state.error_count}")
            lines.append(f"  {'  '.join(parts)}")
        lines.append("")
        lines.append("[bold]无人机详情:[/]")
        for mac, u in state.uavs.items():
            age = time.time() - u.last_seen
            status_icon = "🟢" if age < 300 else "🔴"
            uas_id = ""
            if u.basic_id:
                uas_id = u.basic_id.get("uas_id", "") or ""
            line = f"  {status_icon} [cyan]{escape_markup(mac)}[/]  RSSI={u.rssi}  msgs={u.msg_count}"
            if uas_id:
                line += f"  ID=[bold]{escape_markup(uas_id)}[/]"
            if age >= 300:
                line += f"  [yellow]({age:.0f}s 未更新)[/]"
            lines.append(line)
            if u.location and u.location.get("latitude") is not None:
                loc = u.location
                loc_str = f"     📍 {loc['latitude']:.6f}, {loc['longitude']:.6f}"
                if loc.get("alt_baro") is not None:
                    loc_str += f" alt={loc['alt_baro']:.0f}m"
                if loc.get("speed_h") is not None:
                    loc_str += f" {loc['speed_h']:.1f}m/s"
                if loc.get("direction") is not None:
                    loc_str += f" →{loc['direction']:.0f}°"
                lines.append(loc_str)
    return Panel(Text.from_markup("\n".join(lines)), title="会话摘要", border_style="cyan", padding=(1, 2))


# ── 串口读取线程 ──────────────────────────────────────────

def serial_reader(port, baud):
    """后台线程：从串口读取 JSON 行，所有数据都放入队列（由主线程按模式过滤）"""
    try:
        ser = serial.Serial(port, baud, timeout=1)
    except serial.SerialException as e:
        event_queue.put(("error", f"无法打开串口: {e}"))
        return

    ser.reset_input_buffer()
    buffer = ""

    while state.running:
        try:
            data = ser.read(ser.in_waiting or 1).decode("utf-8", errors="replace")
        except serial.SerialException:
            time.sleep(0.1)
            continue

        if not data:
            continue

        buffer += data
        while "\n" in buffer:
            line, buffer = buffer.split("\n", 1)
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                event_queue.put(("event", obj))
            except json.JSONDecodeError:
                event_queue.put(("raw", line))

    ser.close()


# ── 键盘输入（非阻塞）──────────────────────────────────────

def get_key_nonblock():
    """非阻塞读取单个按键，返回 None 表示无输入"""
    if not console.is_terminal:
        return None
    try:
        import select as _select

        fd = sys.stdin.fileno()
        if _select.select([fd], [], [], 0.1)[0]:
            return os.read(fd, 1).decode("utf-8", errors="replace")
    except Exception:
        pass
    return None


# ── 主入口 ────────────────────────────────────────────────

def find_esp32_port():
    """自动查找 ESP32 串口"""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if "usbmodem" in port.device or "usbserial" in port.device:
            return port.device
        desc = (port.description or "") + (port.manufacturer or "")
        if "CP210" in desc or "CH340" in desc or "CH9102" in desc or "Silicon Labs" in desc:
            return port.device
    return None


def cycle_mode():
    """循环切换模式"""
    modes = ["data", "debug", "static", "all"]
    idx = modes.index(state.mode)
    state.mode = modes[(idx + 1) % len(modes)]
    with state.lock:
        state.log_lines.append(f"[dim]切换到模式: {state.get_mode_desc()}[/]")


def main():
    parser = argparse.ArgumentParser(description="ESP32 Remote ID Scanner — TUI 监视器")
    parser.add_argument("--port", "-p", help="串口路径")
    parser.add_argument("--baud", "-b", type=int, default=115200, help="波特率 (默认: 115200)")
    parser.add_argument("--mode", "-m", choices=["data", "debug", "static", "all"], default="data",
                        help="显示模式 (默认: data)")
    args = parser.parse_args()

    state.mode = args.mode

    port = args.port
    if not port:
        port = find_esp32_port()
        if not port:
            console.print("[red]未找到 ESP32 串口，请用 --port 指定[/]")
            console.print("可用串口:")
            for p in serial.tools.list_ports.comports():
                console.print(f"  {p.device} — {p.description}")
            sys.exit(1)
        console.print(f"[dim]自动检测到串口: {port}[/]")

    console.print(f"[cyan]连接 {port} @ {args.baud} baud ...[/]")
    console.print(f"[cyan]模式: {state.get_mode_desc()}  (按 m 切换 | q 退出)[/]")

    # 启动串口读取线程
    reader_thread = threading.Thread(target=serial_reader, args=(port, args.baud), daemon=True)
    reader_thread.start()

    # 等待串口连接确认
    time.sleep(1)
    try:
        err = event_queue.get_nowait()
        if err[0] == "error":
            console.print(f"[red]{err[1]}[/]")
            sys.exit(1)
    except Empty:
        pass

    # 初始化 TUI（先显示等待画面，非 screen 模式避免 keyboard 冲突）
    try:
        with Live(render_layout(), console=console, refresh_per_second=0.5, screen=False) as live:
            while state.running:
                # 处理事件队列
                try:
                    while True:
                        typ, payload = event_queue.get_nowait()
                        if typ == "event":
                            process_event(payload)
                        elif typ == "raw":
                            with state.lock:
                                state.log_lines.append(f"[dim]{escape_markup(payload)}[/]")
                                if len(state.log_lines) > 200:
                                    state.log_lines = state.log_lines[-150:]
                        elif typ == "error":
                            live.stop()
                            console.print(f"[red]{payload}[/]")
                            sys.exit(1)
                except Empty:
                    pass

                # 渲染
                if state.mode == "debug":
                    live.update(build_debug_view())
                elif state.mode == "static":
                    live.update(build_static_view())
                else:
                    live.update(render_layout())

                # 检查键盘输入
                ch = get_key_nonblock()
                if ch:
                    if ch == 'q':
                        state.running = False
                    elif ch == 'm':
                        cycle_mode()
                    elif ch == 's':
                        live.stop()
                        console.print(build_summary())
                        console.print("\n[dim]按 Enter 继续...[/]")
                        input()
                        live.start()
                    elif ch == 'r':
                        with state.lock:
                            state.log_lines = ["[dim]已刷新[/]"]

    except KeyboardInterrupt:
        pass

    # 退出摘要
    state.running = False
    console.print()
    console.print(build_summary())
    console.print(f"\n[green]已退出[/]")


if __name__ == "__main__":
    main()
