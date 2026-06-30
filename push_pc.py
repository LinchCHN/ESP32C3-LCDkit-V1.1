#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
推送给 ESP32-C3 LCDkit:电脑 CPU/GPU/内存 占用。
设备 POST /push 接收 {"cpu":C,"gpu":G,"mem":M}。IP 靠 ARP+MAC 自动找(设备重启换 IP 也能找)。
依赖: pip install psutil requests pynvml (pynvml 仅 NVIDIA 需要,没有 GPU 显示 --)
用法: python push_pc.py

注:AI 用量功能已停用(GLM/Kimi 无查额 API),本脚本只推电脑占用。
"""
import socket, subprocess, re, time
import psutil, requests

DEVICE_MAC = "98-88-e0-d4-d6-b4"   # 设备 WiFi MAC,看串口 "wifi:mode sta (xx:xx:...)" 那行
PUSH_INTERVAL = 60


def local_subnet():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0].rsplit(".", 1)[0]
    finally:
        s.close()


def find_device_ip(mac):
    """ARP 找设备 MAC 对应 IP。并发 ping 整网段触发 ARP。
    找不到返回 None(不 fallback 固定 IP,避免设备重启换 IP 后连不上)。"""
    sub = local_subnet()
    for i in range(1, 255):
        subprocess.Popen("ping -n 1 -w 100 " + sub + "." + str(i),
                         shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
    try:
        out = subprocess.check_output("arp -a", shell=True, timeout=5).decode(errors="ignore")
    except Exception:
        return None
    norm = mac.lower().replace("-", "").replace(":", "")
    for line in out.splitlines():
        if norm in line.lower().replace("-", "").replace(":", ""):
            m = re.search(r"(\d+\.\d+\.\d+\.\d+)", line)
            if m:
                return m.group(1)
    return None


def gpu_pct():
    try:
        import pynvml
        pynvml.nvmlInit()
        u = pynvml.nvmlDeviceGetUtilizationRates(pynvml.nvmlDeviceGetHandleByIndex(0))
        pynvml.nvmlShutdown()
        return int(u.gpu)
    except Exception:
        return -1


def collect():
    return {
        "cpu": int(psutil.cpu_percent(interval=None)),
        "mem": int(psutil.virtual_memory().percent),
        "gpu": gpu_pct(),
    }


def main():
    print("搜索设备 MAC=" + DEVICE_MAC + " ...")
    ip = find_device_ip(DEVICE_MAC)
    url = "http://" + ip + "/push" if ip else None
    if ip:
        print("设备 IP=" + ip + ",推送 → " + url + "  (每 " + str(PUSH_INTERVAL) + "s)")
    else:
        print("未找到设备(ARP 无此 MAC),将每周期重试")
    psutil.cpu_percent(interval=None)
    while True:
        if not url:
            ip = find_device_ip(DEVICE_MAC)
            if ip:
                url = "http://" + ip + "/push"
                print("找到设备 IP=" + ip)
        if url:
            try:
                data = collect()
                r = requests.post(url, json=data, timeout=5)
                print("CPU %d%%  RAM %d%%  GPU %d%%  → %s" % (
                    data["cpu"], data["mem"], data["gpu"], r.text.strip()))
            except Exception as e:
                print("推送失败: " + str(e) + " → 重新查找设备")
                url = None
        time.sleep(PUSH_INTERVAL)


if __name__ == "__main__":
    main()
