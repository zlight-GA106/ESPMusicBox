# -*- coding: utf-8 -*-
"""ESP Music Box 模拟器全接口自动化验证脚本

对运行中的模拟器执行：
    python verify_simulator.py [base_url]
默认 http://127.0.0.1:8010。可在 pytest 下运行（pytest verify_simulator.py），
也可独立运行并输出 PASS/FAIL 汇总（exit code 0 表示全部通过）。
"""
from __future__ import annotations

import sys
import time
import unittest
import requests

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8010"


class SimulatorApiTest(unittest.TestCase):
    """基类辅助"""
    base = BASE.rstrip("/")

    def get(self, path, **kw):
        return requests.get(self.base + path, timeout=10, **kw)

    def put(self, path, **kw):
        return requests.put(self.base + path, timeout=10, **kw)

    def post(self, path, **kw):
        return requests.post(self.base + path, timeout=10, **kw)

    def delete(self, path, **kw):
        return requests.delete(self.base + path, timeout=10, **kw)

    def json_fields(self, payload, required):
        for f in required:
            self.assertIn(f, payload, "缺少字段 %r -> %r" % (f, payload))


class TestInfo(unittest.TestCase):
    def test_info(self):
        r = requests.get(BASE + "/api/info", timeout=10)
        self.assertEqual(r.status_code, 200)
        d = r.json()
        for f in ("device_name", "mac", "firmware_version", "flash_size"):
            self.assertIn(f, d, d)
        self.assertIsInstance(d["flash_size"], int)
        self.assertGreater(d["flash_size"], 0)
        self.assertEqual(len(d["mac"].split(":")), 6)


class TestStatus(unittest.TestCase):
    def test_status_fields(self):
        r = requests.get(BASE + "/api/status", timeout=10)
        self.assertEqual(r.status_code, 200)
        d = r.json()
        for f in ("lux", "mode", "playback", "playback_source", "playback_loop",
                  "wifi_connected", "ip_address", "littlefs_total", "littlefs_free",
                  "last_error"):
            self.assertIn(f, d, d)
        self.assertIsInstance(d["lux"], (int, float))
        self.assertIn(d["playback"], ("stopped", "buffering", "playing", "error"))
        self.assertIn(d["mode"], ("birthday", "radio"))
        self.assertIsInstance(d["littlefs_free"], int)


class TestConfig(unittest.TestCase):
    def setUp(self):
        self.base = BASE.rstrip("/")
        # 恢复已知基准配置，避免其他测试污染
        requests.put(self.base + "/api/config", json={
            "mode": "birthday", "trigger_direction": "above",
            "trigger_lux": 300, "dead_zone_lux": 50, "birthday_count": 1,
            "birthday_file": "birthday.wav", "volume": 80,
            "play_on_boot": False, "play_boot_loop": False,
            "wifi_ssid": "", "wifi_password": "", "radio_url": "",
        }, timeout=10)

    def test_config_get(self):
        r = requests.get(self.base + "/api/config", timeout=10)
        self.assertEqual(r.status_code, 200)
        d = r.json()
        for f in ("mode", "wifi_ssid", "wifi_password", "volume", "trigger_direction",
                  "trigger_lux", "dead_zone_lux", "radio_url", "birthday_count",
                  "birthday_file", "play_on_boot", "play_boot_loop"):
            self.assertIn(f, d, d)

    def test_config_put_partial_and_get(self):
        r = requests.put(self.base + "/api/config", json={"volume": 42}, timeout=10)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json()["ok"])
        d = requests.get(self.base + "/api/config", timeout=10).json()
        self.assertEqual(d["volume"], 42)

    def test_config_put_invalid_returns_400(self):
        for patch, reason in [
            ({"volume": 999}, "volume 越界"),
            ({"mode": "party"}, "mode 非法"),
            ({"trigger_lux": 0}, "trigger_lux 越界"),
            ({"birthday_count": 101}, "birthday_count 越界"),
            ({"play_on_boot": "yes"}, "play_on_boot 类型错误"),
            ({"trigger_direction": "sideways"}, "direction 非法"),
            ({"birthday_file": "bad name.wav"}, "birthday_file 非法"),
        ]:
            r = requests.put(self.base + "/api/config", json=patch, timeout=10)
            self.assertEqual(r.status_code, 400, "%s: %s" % (reason, r.text))
            self.assertIn("error", r.json())

    def test_config_dead_zone_clamped_when_ge_threshold(self):
        r = requests.put(self.base + "/api/config",
                         json={"trigger_lux": 200, "dead_zone_lux": 300}, timeout=10)
        self.assertEqual(r.status_code, 200, r.text)
        d = requests.get(self.base + "/api/config", timeout=10).json()
        self.assertLess(d["dead_zone_lux"], 200)

    def test_config_bad_json(self):
        r = requests.put(self.base + "/api/config", data=b"{not json", timeout=10)
        self.assertEqual(r.status_code, 400, r.text)
        self.assertIn("error", r.json())


class TestFiles(unittest.TestCase):
    def setUp(self):
        self.base = BASE.rstrip("/")

    def test_list_has_samples(self):
        r = requests.get(self.base + "/api/files", timeout=10)
        self.assertEqual(r.status_code, 200)
        files = {f["name"]: f["size"] for f in r.json()}
        self.assertIn("birthday.wav", files)
        self.assertIn("test.wav", files)
        for f in r.json():
            self.assertIn("name", f)
            self.assertIn("size", f)

    def test_upload_list_delete(self):
        data = b"RIFF\x24\x00\x00\x00WAVEfmt dummy upload payload"
        # 非法扩展名 -> 400
        name = "upload_test.bin"
        r = requests.post(self.base + "/api/files?name=" + name, data=data, timeout=10)
        self.assertEqual(r.status_code, 400, r.text)
        name = "upload_test.wav"
        r = requests.post(self.base + "/api/files?name=" + name, data=data, timeout=10)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json()["ok"])
        files = {f["name"] for f in requests.get(self.base + "/api/files", timeout=10).json()}
        self.assertIn(name, files)
        # 上传空 body -> 400
        r = requests.post(self.base + "/api/files?name=" + name, data=b"", timeout=10)
        self.assertEqual(r.status_code, 400, r.text)
        # 删除
        r = requests.delete(self.base + "/api/files?name=" + name, timeout=10)
        self.assertEqual(r.status_code, 200, r.text)
        files = {f["name"] for f in requests.get(self.base + "/api/files", timeout=10).json()}
        self.assertNotIn(name, files)
        # 删除不存在的文件 -> 404
        r = requests.delete(self.base + "/api/files?name=" + name, timeout=10)
        self.assertEqual(r.status_code, 404, r.text)


class TestPlayback(unittest.TestCase):
    def test_play_stop(self):
        base = BASE.rstrip("/")
        requests.post(base + "/api/stop", timeout=10)
        r = requests.post(base + "/api/play", json={"name": "test.wav", "loop": False}, timeout=10)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json()["ok"])
        self.assertEqual(requests.get(base + "/api/status", timeout=10).json()["playback"], "playing")
        r = requests.post(base + "/api/stop", timeout=10)
        self.assertEqual(r.status_code, 200, r.text)
        st = requests.get(base + "/api/status", timeout=10).json()
        self.assertEqual(st["playback"], "stopped")
        self.assertEqual(st["playback_source"], "")

    def test_play_missing_file_400(self):
        base = BASE.rstrip("/")
        r = requests.post(base + "/api/play", json={"name": "nope.wav"}, timeout=10)
        self.assertEqual(r.status_code, 400, r.text)
        self.assertIn("error", r.json())

    def test_play_loop_flag(self):
        base = BASE.rstrip("/")
        requests.post(base + "/api/stop", timeout=10)
        requests.post(base + "/api/play", json={"name": "test.wav", "loop": True}, timeout=10)
        st = requests.get(base + "/api/status", timeout=10).json()
        self.assertEqual(st["playback"], "playing")
        self.assertTrue(st["playback_loop"])
        requests.post(base + "/api/stop", timeout=10)

    def test_play_stream_url(self):
        base = BASE.rstrip("/")
        requests.post(base + "/api/stop", timeout=10)
        r = requests.post(base + "/api/play",
                          json={"name": "https://example.com/radio.mp3", "loop": True}, timeout=10)
        self.assertEqual(r.status_code, 200, r.text)
        st = requests.get(base + "/api/status", timeout=10).json()
        self.assertEqual(st["playback"], "buffering")
        time.sleep(1.7)
        st = requests.get(base + "/api/status", timeout=10).json()
        self.assertEqual(st["playback"], "playing")
        self.assertTrue(st["playback_source"].startswith("http"))
        requests.post(base + "/api/stop", timeout=10)


class TestTrigger(unittest.TestCase):
    """生日触发 + 死区锁存（ledar 复刻固件语义）"""

    BASE = BASE.rstrip("/")

    def configure(self, direction="above", threshold=300, dead=50):
        r = requests.put(self.BASE + "/api/config", json={
            "mode": "birthday", "trigger_direction": direction,
            "trigger_lux": threshold, "dead_zone_lux": dead,
            "birthday_count": 1, "birthday_file": "birthday.wav",
            "play_on_boot": False, "play_boot_loop": False,
        }, timeout=10)
        assert r.status_code == 200, r.text

    def set_lux(self, value):
        r = requests.post(self.BASE + "/api/sim/lux", json={"lux": value}, timeout=10)
        assert r.status_code == 200, r.text

    def wait_playback(self, want, timeout=8):
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            st = requests.get(self.BASE + "/api/status", timeout=10).json()
            last = st["playback"]
            if last == want:
                return st
            time.sleep(0.3)
        raise AssertionError("playback 未变为 %r, 当前 %r (lux=%r)" %
                             (want, last, requests.get(self.BASE + "/api/status", timeout=10).json().get("lux")))

    def test_above_trigger_and_rearm(self):
        self.set_lux(10)  # 先固定低亮度，防盗触发
        time.sleep(0.6)   # 保证触发线程采样到低光并重新武装
        self.configure("above", 300, 50)
        requests.post(self.BASE + "/api/stop", timeout=10)
        self.assertEqual(requests.get(self.BASE + "/api/status", timeout=10).json()["playback"], "stopped")
        self.set_lux(1200)  # 超过 300 -> 应触发 birthday.wav
        st = self.wait_playback("playing")
        self.assertEqual(st["playback_source"], "birthday.wav")
        # 触发后锁存：lux 小幅回落到 260（300-50=250 之上）不应再次触发
        requests.post(self.BASE + "/api/stop", timeout=10)
        self.set_lux(260)
        time.sleep(1.2)
        self.assertEqual(requests.get(self.BASE + "/api/status", timeout=10).json()["playback"], "stopped")
        # 慢慢回到死区内 -> 重新武装，再触发一次
        self.set_lux(200)  # 200 <= 250，武装
        time.sleep(0.6)
        self.set_lux(900)
        st = self.wait_playback("playing", timeout=10)
        self.assertEqual(st["playback_source"], "birthday.wav")
        requests.post(self.BASE + "/api/stop", timeout=10)

    def test_below_trigger_and_rearm(self):
        self.set_lux(400)  # 先固定高亮度，防盗触发
        self.configure("below", 300, 50)
        requests.post(self.BASE + "/api/stop", timeout=10)
        time.sleep(1.0)
        self.assertEqual(requests.get(self.BASE + "/api/status", timeout=10).json()["playback"], "stopped")
        self.set_lux(100)  # 低于 300 -> 触发
        st = self.wait_playback("playing")
        self.assertEqual(st["playback_source"], "birthday.wav")
        requests.post(self.BASE + "/api/stop", timeout=10)
        self.set_lux(320)  # 340 之上才重新武装，320 仍在死区
        time.sleep(1.2)
        self.assertEqual(requests.get(self.BASE + "/api/status", timeout=10).json()["playback"], "stopped")
        self.set_lux(400)  # 重新武装
        time.sleep(0.6)
        self.set_lux(50)   # 再次触发
        st = self.wait_playback("playing")
        self.assertEqual(st["playback_source"], "birthday.wav")
        requests.post(self.BASE + "/api/stop", timeout=10)

    def test_play_and_stop_is_stopped(self):
        self.configure("above", 300, 50)
        requests.post(self.BASE + "/api/stop", timeout=10)
        self.set_lux(10)
        self.assertEqual(requests.get(self.BASE + "/api/status", timeout=10).json()["playback"], "stopped")


def main():
    """独立运行：输出 PASS/FAIL 汇总，任意失败退出码为 1"""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2, stream=sys.stdout)
    result = runner.run(suite)
    total = result.testsRun
    failed = len(result.failures) + len(result.errors)
    print("\n" + "=" * 60)
    print("TOTAL=%d  PASS=%d  FAIL=%d" % (total, total - failed, failed))
    if failed:
        print("RESULT: FAIL")
        sys.exit(1)
    print("RESULT: PASS")


if __name__ == "__main__":
    main()
