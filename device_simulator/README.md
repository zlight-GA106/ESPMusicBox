# ESP Music Box 设备模拟器

复刻固件 `main/web_api/web_api.c` 的全部 HTTP 接口与生日触发锁存逻辑，
供 Android 配置软件在没有真实 ESP32-S3 时联调。状态存内存，音频文件保存在
`device_simulator/data/` 目录（首次启动自动生成 `birthday.wav`、`test.wav` 两个演示音频）。

## 环境

- Python 3.9+（本机开发验证用 3.13）
- 依赖：`fastapi` + `uvicorn`（接口为纯 JSON + 二进制上传，FastAPI 的模型校验与
  自动文档最省事；代价是多两个传递依赖，可接受），验证脚本另需 `requests`/`pytest`

```powershell
pip install -r device_simulator/requirements.txt
```

## 启动

```powershell
python device_simulator/simulator.py                # 默认 0.0.0.0:8010
python device_simulator/simulator.py --port 8010    # 自定义端口
```

浏览器打开 http://127.0.0.1:8010/docs 可看 So 台体 API 文档。

- 默认配置：birthday 模式、above 方向、触发 300 Lux、死区 50 Lux、生日歌 `birthday.wav`
- Android 模拟器里访问宿主机请用 `http://10.0.2.2:8010/`（base URL 要带端口）
- 真机与模拟器同网段时访问 `http://<电脑IP>:8010/`

## 设备状态说明

`POST /api/sim/lux` 可强制设定当前光照，两个用途：

1. 手动演示触发（设 lux 越过阈值 → playback 变 `playing`，播放 `birthday_file`）
2. 暂停/不暂停随机游走：`{"lux": 1200}` 会暂停随机游走（之后 lux 固定为设定值，
   触发演示可重复）；`{"lux": 1200, "auto": true}` 设定后恢复每 2 秒随机游走

`GET /api/sim/lux` 返回当前 lux 与是否在自动游走；
`POST /api/sim/auto` 直接恢复随机游走。

`/api/status`、`/api/config`、`/api/files`、`/api/play`、`/api/stop`、`/api/ota`
与固件行为一致（含 400/404/409/501 错误码与固件同款字段校验、`playback`
状态机 stopped/buffering/playing/error）。

## 验证（全接口自动化断言）

先启动模拟器，再执行（另开终端）：

```powershell
python device_simulator/verify_simulator.py
```

输出 `RESULT: PASS`、退出码 0 即全过。也可按 pytest 方式运行：

```powershell
pytest device_simulator/verify_simulator.py -v
```

覆盖：info/status/config 读写、非法字段 400、文件上传/列表/删除、
播放/停止/循环/流媒体、above & below 触发 + 死区锁存 + 重新武装。

## 触发演示步骤（curl）

```powershell
# 1. 启动模拟器（终端 A）
python device_simulator/simulator.py

# 2. 固定为低光，确认已停止（终端 B）
curl http://127.0.0.1:8010/api/sim/lux -H "Content-Type: application/json" -d "{\"lux\":10}"
curl http://127.0.0.1:8010/api/status

# 3. 设定高光超过阈值（默认 above 300 Lux）→ 秒内播放 birthday.wav
curl http://127.0.0.1:8010/api/sim/lux -H "Content-Type: application/json" -d "{\"lux\":1200}"
curl http://127.0.0.1:8010/api/status        # playback = "playing", playback_source = "birthday.wav"

# 4. 回到死区内（<= 300-50=250）重新武装，再升高可再次触发
curl http://127.0.0.1:8010/api/sim/lux -H "Content-Type: application/json" -d "{\"lux\":200}"
curl http://127.0.0.1:8010/api/sim/lux -H "Content-Type: application/json" -d "{\"lux\":900}"
curl http://127.0.0.1:8010/api/status        # 再次 playing
```
