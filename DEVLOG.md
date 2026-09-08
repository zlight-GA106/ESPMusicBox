# ESP Music Box 开发日志（DEVLOG）

> Android 配置软件 + 设备模拟器。本文件按时间追加，记录进度、遇到的问题与决策理由。

---

## 2026-09-09 会话开始

### 环境勘察（开测前）

| 项目 | 结果 |
|---|---|
| 任务指定仓库路径 `C:\Users\zlight106\Desktop\espmusicbox` | **不存在**（本机用户为 `zlihome`，无 `zlight106` 用户目录） |
| 实际仓库 | `D:\opencode project\ESPMusicBox`（与任务同一 GitHub 源，工作区已克隆于上一个会话，`git status` 干净） |
| **决策** | 本地工作目录定为 `D:\opencode project\ESPMusicBox`，DEVLOG 也放该仓库根目录；README 中相关安装时路径写成相对路径。理由：任务描述来自另一台机器，本机仓库即为同一仓库，避免二次克隆产生双份工作区。 |
| git | `C:\Program Files\Git\cmd\git.exe`，仓库已有历史（5 个提交，main 分支） |
| Python | `Python 3.13.15` + pip 26.2.1 ✓ |
| Android Studio | `D:\Android\Android Studio`（2026.1.3，含 `jbr` 内置 JDK 21）✓ |
| JDK（studio jbr） | `D:\Android\Android Studio\jbr\bin\java.exe` ✓（可当 JAVA_HOME 用） |
| **Android SDK** | **缺失**：`%LOCALAPPDATA%\Android\Sdk` 不存在，全盘未找到 platform-tools/cmdline-tools/adb，无 AVD |
| Android Studio 历史 | 仅 first-run 状态（`C:\Users\zlihome\.android\studio\installer\firstrun.data`），SDK 组件从未安装 |
| **决策** | 不用 winget（交互少、包源慢）；直接从 `dl.google.com` 下载 **cmdline-tools**（`commandlinetools-win-11076708_latest.zip`），解压到 `D:\Android\Sdk\cmdline-tools\latest`，用 `sdkmanager` 装 `platform-tools`、`platforms;android-35`、`build-tools;34.0.0`，写成 `scripts/setup_android_env.ps1` 留档；JDK 直接用 Studio 自带 `jbr`。 |

### 模拟器技术选型

FastAPI + uvicorn（任务允许 Flask，选 FastAPI 说明：接口为纯 JSON + 二进制上传，FastAPI 的 Pydantic `/api/config` 校验与自带 OpenAPI 可读文档最省事，且后续要扩 WebSocket 状态推送也有现成支持；缺点是多两个传递依赖，可接受）。

### Python 依赖安装

```
fastapi uvicorn[standard] requests pytest
```

（`uvicorn[standard]` 仅为开发舒服，装不上就降级为 `uvicorn`。）

---

## 里程碑 M1：device_simulator

### 已完成

- `device_simulator/simulator.py`（FastAPI + uvicorn，约 500 行，见技术选型）：复刻
  web_api.c 全部接口（含 400/404/409/501 错误码、config 固件同款字段校验与
  "dead_zone_lux >= trigger_lux 静默钳制 0.2 倍" 行为、文件名 ASCII 安全校验
  `.wav/.mp3`、文件上传原子替换（先写 `.upload` 再 rename）、流媒体 buffering→playing、
  上电播放语义 play_on_boot / play_boot_loop）。
- 触发引擎：above/below + dead_zone 死区锁存，0.25s 轮询；显示即真实联动。
- 光照仿真：启动后每 2s ±400Lux 随机游走（0..2000）。`POST /api/sim/lux {"lux":x}`
  强制设值并**暂停游走**（保证演示/测试可重复），`{"lux":x,"auto":true}` 恢复游走；
  同接口 GET 可查询当前值。
- 演示音频：`data/birthday.wav`（2s 440Hz 正弦，PCM16/16k/mono）、`data/test.wav`（1.5s 660Hz）。
- `verify_simulator.py`：16 个用例全过（x3 稳定）覆盖 info/status/config 读写+400、
  文件上传/列表/删除/404、播放/停止/循环/流媒体、above/below 触发+锁存+重新武装。
  独立运行输出 `TOTAL/PASS/FAIL/RESULT`。

### 遇到的问题（均已解决）

1. **首次导入即崩**：FastAPI 实例命名为 `APP`，装饰器用到未定义的 `app`。
   → 加 `app = APP` 别名。
2. **/api/sim/lux 不暂停游走**：2 秒一次的随机游走会覆盖手动设定值，压测时
   lum 值飘走导致断言间歇失败。 → 语义改为"设值即暂停游走，auto:true 恢复"，
   与 README 声明一致；验证脚本在触发测试前先 set_lux 防盗触发。
3. **测试竞态（本次最有价值发现）**：`test_above_trigger_and_rearm` 先 `set_lux(10)`
   再几毫秒内连续 PUT config/stop/GET/`set_lux(1200)`，触发引擎 0.25s 轮询
   **从未采样到 lux=10**，自然无法重新武装，armed 保持 False → 断言失败。
   这不是模拟器 bug：重新武装本来就需要被采样到死区内的值才算数（与固件 BH1750
   采样语义一致）。→ 测试在 set_lux(10) 后 `sleep(0.6)` 留出采样窗口。连带拷打：
   期间在触发引擎加了临时 heartbeat 日志定位（已移除，保留 INFO 级
   TRIGGER FIRED / RE-ARMED 日志便于演示）。

### 边界决策

- 服务器进程后台跑 8010；`device_simulator/server.*.log` 与 `__pycache__` 已进 .gitignore。
- 演示/验证脚本对随机游走的竞态处理：一律先 `set_lux` 固定 → 确定性可重复。
