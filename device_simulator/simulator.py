# -*- coding: utf-8 -*-
"""ESP Music Box 设备模拟器

复刻固件 main/web_api/web_api.c 的全部 HTTP 接口与生日触发锁存逻辑，
供 Android 配置软件在没有真实 ESP32 时联调使用。状态（lux、播放、配置）
全部存在内存，音频文件保存在本目录 data/ 下。

启动：  python simulator.py                 # 默认 0.0.0.0:8010
        python simulator.py --port 8010
"""
from __future__ import annotations

import argparse
import json
import logging
import math
import random
import re
import threading
import time
import wave
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

APP = FastAPI(title="ESP Music Box Simulator", version="1.4.0")
app = APP
log = logging.getLogger("simulator")

SIM_VERSION = "1.4.0"
SIM_DEVICE_NAME = "ESP32-S3 Birthday Music Box"
SIM_MAC = "A4:CF:12:34:56:78"
SIM_FLASH_SIZE = 16 * 1024 * 1024
LITTLEFS_TOTAL = 10_354_688
DATA_DIR = Path(__file__).resolve().parent / "data"

SAFE_NAME_RE = re.compile(r"^[A-Za-z0-9._-]+$")
EXTENSIONS = (".wav", ".mp3")


def safe_music_name(name: str) -> bool:
    """与固件 filesystem_safe_music_name 一致：ASCII 字母/数字/./-/_，非.开头，<97 字符，.wav/.mp3 结尾"""
    if not name or len(name) > 96 or name[0] == ".":
        return False
    if not SAFE_NAME_RE.match(name):
        return False
    return name.lower().endswith(EXTENSIONS)


def music_files() -> list[tuple[str, int]]:
    """返回 [(name, size)]，仅包含合法音乐文件名，按名称排序"""
    out = []
    if DATA_DIR.is_dir():
        for p in DATA_DIR.iterdir():
            if p.is_file() and safe_music_name(p.name):
                out.append((p.name, p.stat().st_size))
    out.sort(key=lambda x: x[0])
    return out


def used_bytes() -> int:
    return sum(sz for _, sz in music_files())


# ---------------------------------------------------------------- 配置（内存态）

CONFIG_DEFAULTS = {
    "mode": "birthday",
    "wifi_ssid": "",
    "wifi_password": "",
    "radio_url": "",
    "volume": 80,
    "trigger_direction": "above",
    "trigger_lux": 300.0,
    "dead_zone_lux": 50.0,
    "birthday_count": 1,
    "birthday_file": "birthday.wav",
    "play_on_boot": False,
    "play_boot_loop": False,
}

config_lock = threading.Lock()
config = dict(CONFIG_DEFAULTS)


def validate_config_update(body: dict) -> Optional[str]:
    """在副本上做固件同款校验；返回 None 表示通过，否则返回错误消息"""
    if not isinstance(body, dict):
        return "JSON object required"
    cand = dict(config)
    if "mode" in body:
        if body["mode"] not in ("birthday", "radio"):
            return "mode must be birthday or radio"
        cand["mode"] = body["mode"]
    for key in ("wifi_ssid", "wifi_password", "radio_url"):
        if key in body:
            v = body[key]
            if not isinstance(v, str) or len(v) >= 64:
                return "%s is invalid or too long" % key
            cand[key] = v
    if "birthday_file" in body:
        v = body["birthday_file"]
        if not isinstance(v, str) or not safe_music_name(v):
            return "birthday_file must be a valid wav or mp3 name"
        cand["birthday_file"] = v
    if "volume" in body:
        v = body["volume"]
        if isinstance(v, bool) or not isinstance(v, (int, float)) or v < 0 or v > 100:
            return "volume must be 0..100"
        cand["volume"] = int(v)
    if "trigger_direction" in body:
        if body["trigger_direction"] not in ("above", "below"):
            return "trigger_direction must be above or below"
        cand["trigger_direction"] = body["trigger_direction"]
    if "trigger_lux" in body:
        v = body["trigger_lux"]
        if isinstance(v, bool) or not isinstance(v, (int, float)) or v < 1 or v > 100000:
            return "trigger_lux must be 1..100000"
        cand["trigger_lux"] = float(v)
    if "dead_zone_lux" in body:
        v = body["dead_zone_lux"]
        if isinstance(v, bool) or not isinstance(v, (int, float)) or v < 0 or v > 100000:
            return "dead_zone_lux must be 0..100000"
        cand["dead_zone_lux"] = float(v)
    if "birthday_count" in body:
        v = body["birthday_count"]
        if isinstance(v, bool) or not isinstance(v, (int, float)) or v < 1 or v > 100:
            return "birthday_count must be 1..100"
        cand["birthday_count"] = int(v)
    for key in ("play_on_boot", "play_boot_loop"):
        if key in body:
            if not isinstance(body[key], bool):
                return "%s must be boolean" % key
            cand[key] = body[key]
    # 固件行为：dead_zone_lux >= trigger_lux 时静默钳制为 trigger_lux * 0.2
    if cand["dead_zone_lux"] >= cand["trigger_lux"]:
        cand["dead_zone_lux"] = round(cand["trigger_lux"] * 0.2, 2)
    config_lock.acquire()
    try:
        config.update(cand)
    finally:
        config_lock.release()
    return None


def config_snapshot() -> dict:
    with config_lock:
        return dict(config)


# ---------------------------------------------------------------- 光照仿真

class LuxSimulator:
    """默认每 2 秒随机游走（0..2000 Lux）；POST /api/sim/lux 强制设定并可暂停游走"""

    INTERVAL = 2.0
    STEP = 400.0
    MIN, MAX = 0.0, 2000.0

    def __init__(self):
        self.lock = threading.Lock()
        self.lux = 500.0
        self.auto_walk = True
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        while not self._stop.is_set():
            self._stop.wait(self.INTERVAL)
            with self.lock:
                if self.auto_walk:
                    self.lux = round(min(self.MAX, max(self.MIN, self.lux + random.uniform(-self.STEP, self.STEP))), 1)

    def get(self) -> float:
        with self.lock:
            return self.lux

    def set(self, value: float, auto_walk: Optional[bool] = None) -> float:
        with self.lock:
            self.lux = round(min(self.MAX, max(self.MIN, float(value))), 1)
            if auto_walk is not None:
                self.auto_walk = bool(auto_walk)
            return self.lux

    def resume_walk(self):
        with self.lock:
            self.auto_walk = True


lux_sim = LuxSimulator()


# ---------------------------------------------------------------- 播放引擎

def wav_duration_seconds(path: Path) -> Optional[float]:
    """从 WAV 头估计时长；非 WAV 或损坏文件返回 None"""
    try:
        with wave.open(str(path), "rb") as w:
            frames = w.getnframes()
            rate = w.getframerate()
            return frames / rate if rate > 0 else None
    except Exception:
        return None


class PlaybackEngine:
    """单线程播放模拟：本地文件按时长播放，http(s) URL 模拟永远流播"""

    BUFFERING_SECONDS = 1.5
    MP3_FALLBACK_SECONDS = 30.0

    def __init__(self, data_dir: Path):
        self.data_dir = data_dir
        self.lock = threading.Lock()
        self._stop = threading.Event()
        self._job = threading.Event()
        self._name = ""
        self._loop = False
        self._count = 1
        self.playback = "stopped"  # stopped/buffering/playing/error
        self.source = ""
        self.loop = False
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _set(self, playback: str, source: str, loop: bool):
        with self.lock:
            self.playback = playback
            self.source = source
            self.loop = loop

    def snapshot(self) -> tuple[str, str, bool]:
        with self.lock:
            return self.playback, self.source, self.loop

    def _duration(self, name: str) -> float:
        if name.startswith(("http://", "https://")):
            return -1.0  # 流式
        p = self.data_dir / name
        if not p.is_file():
            return None
        dur = wav_duration_seconds(p)
        if dur is not None:
            return dur
        return self.MP3_FALLBACK_SECONDS

    def play(self, name: str, loop: bool = False, count: int = 1):
        with self.lock:
            self._stop.set()
            self._name = name
            self._loop = loop
            self._count = count
            self._job.set()

    def stop(self):
        self._job.set()
        with self.lock:
            self._stop.set()
            self._name = ""

    def _run(self):
        while True:
            self._job.wait()
            self._job.clear()
            with self.lock:
                name, loop, count = self._name, self._loop, self._count
            if not name:
                self._set("stopped", "", False)
                continue
            stream = name.startswith(("http://", "https://"))
            self._stop.clear()
            self._set("buffering", name, loop)
            if stream:
                if self._stop.wait(self.BUFFERING_SECONDS):
                    self._set("stopped", "", False)
                    continue
                self._set("playing", name, loop)
                self._stop.wait()
                self._set("stopped", "", False)
                continue
            duration = self._duration(name)
            if duration is None:
                self._set("error", name, loop)
                self._stop.wait(2.0)
                self._set("stopped", "", False)
                continue
            remaining = -1 if loop else max(1, count)
            stopped = False
            while remaining != 0 and not stopped:
                self._set("playing", name, loop)
                if remaining > 0:
                    remaining -= 1
                stopped = self._stop.wait(duration)
            if stopped:
                self._stop.clear()
            self._set("stopped", "", False)


engine = PlaybackEngine(DATA_DIR)


# ---------------------------------------------------------------- 触发引擎（锁存逻辑）

class TriggerEngine:
    """固件生日触发语义：
    - above: lux > trigger_lux 触发；触发后需 lux <= trigger_lux - dead_zone_lux 才重新武装
    - below: lux < trigger_lux 触发；触发后需 lux >= trigger_lux + dead_zone_lux 才重新武装
    触发时播放 birthday_file 共 birthday_count 次。"""

    CHECK_INTERVAL = 0.25

    def __init__(self):
        self.armed = True
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        while not self._stop.is_set():
            self._stop.wait(self.CHECK_INTERVAL)
            cfg = config_snapshot()
            if cfg["mode"] != "birthday":
                continue
            direction = cfg["trigger_direction"]
            threshold = cfg["trigger_lux"]
            dead = cfg["dead_zone_lux"]
            cur = lux_sim.get()
            if self.armed:
                fire = (direction == "above" and cur > threshold) or \
                       (direction == "below" and cur < threshold)
                if fire:
                    log.info("TRIGGER FIRED dir=%s lux=%.1f threshold=%.1f file=%s count=%d",
                             direction, cur, threshold, cfg["birthday_file"], cfg["birthday_count"])
                    self.armed = False
                    engine.play(cfg["birthday_file"], loop=False, count=cfg["birthday_count"])
            else:
                rearmed = (direction == "above" and cur <= threshold - dead) or \
                          (direction == "below" and cur >= threshold + dead)
                if rearmed:
                    log.info("RE-ARMED dir=%s lux=%.1f threshold=%.1f dead=%.1f",
                             direction, cur, threshold, dead)
                    self.armed = True


trigger_engine = TriggerEngine()


# ---------------------------------------------------------------- HTTP 接口

def err_response(status: str, message: str) -> JSONResponse:
    return JSONResponse({"ok": False, "error": message}, status_code=int(status.split()[0]) if status.split() else status)


@app.get("/")
def root():
    return {"service": "ESP Music Box Simulator (HTTP API of ESP32-S3 Birthday Music Box)",
            "endpoints": ["/api/info", "/api/status", "/api/config", "/api/files",
                          "/api/play", "/api/stop", "/api/ota", "/api/sim/lux"]}


@app.get("/api/info")
def api_info():
    return {"device_name": SIM_DEVICE_NAME, "mac": SIM_MAC,
            "firmware_version": SIM_VERSION, "flash_size": SIM_FLASH_SIZE}


@app.get("/api/status")
def api_status():
    playback, source, loop = engine.snapshot()
    cfg = config_snapshot()
    return {
        "lux": lux_sim.get(),
        "mode": cfg["mode"],
        "playback": playback,
        "playback_source": source,
        "playback_loop": loop,
        "wifi_connected": True,
        "ip_address": "127.0.0.1",
        "littlefs_total": LITTLEFS_TOTAL,
        "littlefs_free": max(0, LITTLEFS_TOTAL - used_bytes()),
        "last_error": "",
    }


@app.get("/api/config")
def api_config_get():
    return config_snapshot()


@app.put("/api/config")
async def api_config_put(request: Request):
    try:
        body = json.loads(await request.body())
    except Exception:
        return err_response("400 Bad Request", "invalid JSON")
    error = validate_config_update(body)
    if error:
        return err_response("400 Bad Request", error)
    return {"ok": True}


@app.get("/api/files")
def api_files_get():
    return [{"name": n, "size": s} for n, s in music_files()]


@app.post("/api/files")
async def api_files_post(request: Request, name: str = ""):
    body = await request.body()
    if not safe_music_name(name) or len(body) == 0:
        return err_response("400 Bad Request", "valid name and non-empty body required")
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    final = DATA_DIR / name
    temp = DATA_DIR / (name + ".upload")
    try:
        temp.write_bytes(body)
        temp.replace(final)
    except OSError as exc:
        return err_response("500 Internal Server Error", str(exc))
    return {"ok": True}


@app.delete("/api/files")
def api_files_delete(name: str = ""):
    if not safe_music_name(name):
        return err_response("400 Bad Request", "valid name required")
    path = DATA_DIR / name
    if not path.is_file():
        return err_response("404 Not Found", "ESP_ERR_NOT_FOUND")
    playback, source, loop = engine.snapshot()
    if source == name:
        engine.stop()
    try:
        path.unlink()
    except OSError as exc:
        return err_response("500 Internal Server Error", str(exc))
    return {"ok": True}


@app.post("/api/play")
async def api_play(request: Request):
    try:
        body = json.loads(await request.body() or b"{}")
    except Exception:
        return err_response("400 Bad Request", "invalid JSON")
    if not isinstance(body, dict) or not isinstance(body.get("name"), str) or not body["name"]:
        return err_response("400 Bad Request", "ESP_ERR_INVALID_ARG")
    name = body["name"]
    loop = bool(body.get("loop", False))
    if not name.startswith(("http://", "https://")) and not (DATA_DIR / name).is_file():
        return err_response("400 Bad Request", "ESP_ERR_NOT_FOUND")
    engine.play(name, loop=loop, count=1)
    return {"ok": True}


@app.post("/api/stop")
def api_stop():
    engine.stop()
    return {"ok": True}


@app.post("/api/ota")
def api_ota():
    return err_response("501 Not Implemented", "OTA endpoint reserved")


@app.post("/api/sim/lux")
async def api_sim_lux(request: Request):
    try:
        body = json.loads(await request.body() or b"{}")
    except Exception:
        return err_response("400 Bad Request", "invalid JSON")
    if not isinstance(body, dict) or "lux" not in body:
        return err_response("400 Bad Request", "lux required")
    if "auto" in body and not isinstance(body["auto"], bool):
        return err_response("400 Bad Request", "auto must be boolean")
    auto = body.get("auto") if "auto" in body else False
    value = lux_sim.set(body["lux"], auto_walk=auto)
    return {"ok": True, "lux": value, "auto_walk": bool(auto)}


@app.get("/api/sim/lux")
def api_sim_lux_get():
    return {"lux": lux_sim.get(), "auto_walk": lux_sim.auto_walk}


@app.post("/api/sim/auto")
def api_sim_auto():
    lux_sim.resume_walk()
    return {"ok": True}


# ---------------------------------------------------------------- 启动

def ensure_samples():
    """生成演示音频：2 秒 440Hz 与 1.5 秒 660Hz 正弦 WAV（PCM16 16kHz mono）"""
    def make(path: Path, seconds: float, frequency: float):
        if path.is_file():
            return
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        rate = 16000
        with wave.open(str(path), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            frames = bytearray()
            for i in range(int(rate * seconds)):
                sample = int(32767 * 0.6 * math.sin(2 * math.pi * frequency * i / rate))
                frames += sample.to_bytes(2, "little", signed=True)
            w.writeframes(bytes(frames))
    make(DATA_DIR / "birthday.wav", 2.0, 440.0)
    make(DATA_DIR / "test.wav", 1.5, 660.0)


def apply_boot_behavior():
    """固件语义：通电后直接播放（生日模式，play_on_boot=true）；play_boot_loop=true 为无限循环"""
    cfg = config_snapshot()
    if cfg["mode"] == "birthday" and cfg["play_on_boot"]:
        engine.play(cfg["birthday_file"], loop=cfg["play_boot_loop"], count=cfg["birthday_count"])


def main():
    parser = argparse.ArgumentParser(description="ESP Music Box device simulator")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8010)
    args = parser.parse_args()
    ensure_samples()
    apply_boot_behavior()
    import uvicorn

    uvicorn.run(APP, host=args.host, port=args.port, log_level="info")


if __name__ == "__main__":
    main()
