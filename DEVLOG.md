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

---

## 里程碑 M2：Android 配置软件

### 环境搭建（脚本 scripts/setup_android_env.ps1，成功执行）

- SDK 组件原本完全缺失（Studio 只装过 IDE）。脚本从 dl.google.com 拉 cmdline-tools
  → sdkmanager 装 `platform-tools` / `platforms;android-35` / `build-tools;34.0.0`
  → `D:\Android\Sdk`。
- **坑 1：sdkmanager 需要 JAVA_HOME** → 脚本注入 jbr（后证明不可用，见坑 2）。
- **坑 2（关键）**：Android Studio 2026.1.3 自带 jbr 是 **JDK 25.0.2**，
  Gradle 8.9 内嵌 Kotlin（intellij JavaVersion）解析 "25.0.2" 直接抛
  `IllegalArgumentException: 25.0.2`（wrapper 任务都跑不了）。
  **决策**：走 Adoptium API 下载 **Temurin JDK 17.0.20.1** 到 `D:\Android\jdk-17`，
  `setx JAVA_HOME` 持久化；构建时显式 `$env:JAVA_HOME`。README 与脚本注释写明原因。
- 测试机：`adb devices` 检测到 **b5b85793（unauthorized）** —— 需要手机弹窗点
  「允许 USB 调试」，桌面侧无法代点（已记入"用户手动动作"清单）。

### 技术栈（决定并执行）

- Gradle 8.9（wrapper 由下载的发行版内联生成）+ AGP 8.7.3 + Kotlin 2.0.21
  + Compose compiler plugin + kotlinx.serialization 2.0.21；BOM 2024.12.01；
  OkHttp 4.12.0；minSdk 26 / target & compile 35。UI 零第三方（仅 Compose/Material3）。
- 单 Activity + 底部导航三页签；AppViewModel 内存态 + 3 秒轮询状态页。
- baseUrl 存 SharedPreferences；JSON snake_case 与设备一致。

### 首编失败 → 修复（干净 APK）

`assembleDebug` 首次失败清单：`call()` 泛型传了 Request 却声明 ()->Response（改签名 `call(Request, parse)`）；
`json.toJsonString` 不存在（改手写转义）；`vm.config` 可变属性 smart-cast 被拒（改局部 val）；
MainActivity `remember{}` 里的 `LocalContext.current` 非组合上下文（移到外面）；
`ExperimentalMaterial3Api` 未导入。修复后 **BUILD SUCCESSFUL in 38s**，
APK：`android/app/build/outputs/apk/debug/app-debug.apk`（9.5 MB）。

### 验证状态

- [x] `gradlew.bat assembleDebug` 成功产出 APK。
- [ ] 装到测试机：awaiting USB 调试授权（手动步骤，见"用户手动动作"清单）。

---

## 收尾

- `git push origin main` 在本机会话挂起（HTTPS 需要 Windows Credential Manager 交互，
  已用 GIT_TERMINAL_PROMPT=0 限制，超时终止）；**本地提交完整**，push 属于用户手动动作。
- 测试机持续 `unauthorized`；未做 APK 实机安装（模拟器无系统镜像，未下载 ~1.2GB）。

## 明早验收清单逐项勾选

- [x] `device_simulator/simulator.py` 一键启动（`python device_simulator/simulator.py`，
      默认 0.0.0.0:8010），`verify_simulator.py` 全接口 **PASS（16/16，连跑 3 次稳定）**
      —— 覆盖 info/status/config 读写、400 非法字段、文件上传/列表/删除/404、
      播放/停止/循环/流媒体、above & below 触发 + 死区锁存 + 重新武装。
- [x] `android/` 工程 `gradlew.bat assembleDebug` 成功，
      APK：`android/app/build/outputs/apk/debug/app-debug.apk`（9,559,804 B，2026-09-09 01:16）。
- [x] README：`device_simulator/README.md`（启动/验证/curl 演示步骤、
      模拟器访问地址说明）与 `android/README.md`（构建/安装/连接地址/触发演示）。
- [x] `DEVLOG.md`：全程日志 + 本清单。
- [x] Git：`git log` 可查 `04824a7`（模拟器）、`b12e876`（Android 工程+脚本）；
      main 分支、无 amend、未动固件代码（main/、components/ 零改动，只新增）。

## 缺失的环境 / 需要用户手动做的动作（完整清单）

1. **测试机 USB 授权**：手机屏幕弹窗点「允许 USB 调试」→
   `D:\Android\Sdk\platform-tools\adb.exe install -r android\app\build\outputs\apk\debug\app-debug.apk`
   （当前 `adb devices` 显示 b5b85793 unauthorized）。
2. **推送 GitHub（可选）**：`git push origin main`（本会话无凭据交互，未 push）。
3. **（可选）AVD 模拟器**：如需模拟器演示，
   `D:\Android\Sdk\cmdline-tools\latest\bin\sdkmanager.bat "system-images;android-35;google_apis;x86_64"`
   后 `avdmanager create avd ...`（本文件未下载系统镜像）。
4. **环境变量**：`setx JAVA_HOME D:\Android\jdk-17\jdk-17.0.20.1+1`（脚本已执行）；
   SDK 路径在 `android/local.properties`（不入库，换机需改）。
5. 真机演示触发：确保手机与电脑同 WiFi，连 `http://<电脑IP>:8010/`；
   模拟器用 `http://10.0.2.2:8010/`。
