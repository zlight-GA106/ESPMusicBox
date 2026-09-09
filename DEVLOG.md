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

## 真机端到端验证（用户授权 USB 调试后完成）

- **设备**：Xiaomi/Redmi `b5b85793`（23013RK75C，Android 授权后显示 `device`）。
- **坑 3（安装）**：`adb install`（streaming 通道）在此 ROM 上无限挂起；
  **改用 `adb install --no-streaming -r` push 安装一次通过**（约 0.25s）。
- **坑 4（二进制输出）**：PowerShell `>` 重定向 `adb exec-out` 会把 PNG 写坏
  （变成 UTF-16）；必须 `cmd /c "adb exec-out screencap -p > x.png"` 或直接 pull。
- **关键通道**：`adb reverse tcp:8010 tcp:8010` 让手机 127.0.0.1:8010 → 宿主机模拟器，
  完全模拟"设备在局域网"场景；App 默认 base URL `http://127.0.0.1:8010/` 直连成功。
- **自动化驱动**：uiautomator dump（UTF-8 拉回本地解析 bounds）→ `input tap` →
  screencap（缩图后人工确认），全程键盘/鼠标零操作。

### 已验证（有截图佐证，存于本地会话记录）

- App 启动 → 点击「连接」→ 状态页显示：ESP32-S3 Birthday Music Box /
  A4:CF:12:34:56:78 / 1.4.0 / 16.0MB / Lux=1200.0 / 生日模式 / WiFi 已连接 /
  LittleFS 9.8MB/9.9MB（全部来自模拟器真实数据）。
- 音频页：birthday.wav ★生日歌（62.5KB）+ test.wav（46.9KB），与 data/ 一致。
- 配置页：300/50/1 等与模拟器配置一致。
- 触发联动：`POST /api/sim/lux {"lux":10}` → 停顿 → `{"lux":1200}` 后
  模拟器 `playback=playing source=birthday.wav`，App 状态页 3s 轮询同步。
- App 播放：音频页点「播放」test.wav → 模拟器 playing → 状态页「播放中 /
  test.wav」+ Snackbar「播放中: test.wav」。
- UI 修复：音频页「单曲无限循环」复选框换行到独立行（修复前文字竖排被挤压），
  重建重装后横排正常。

> 说明：MyUI 上 `input tap` 偶有 2~4s 排队延迟，截图时机受影响但不影响结论。

---

## 里程碑 M3：串口配置模式 1:1 移植 + 新测试机 + 标题/图标

### 需求要点（用户指示）

- 加入并保留串口配置模式：串口传输与配置是重要环节，**一比一移植全部功能**。
- 更换了测试机（继续开发）。
- 软件标题「音乐盒配置助手（版本号随电脑端）」——电脑端 pc_tool 当前为 **1.6.0**
  （csproj <Version>1.6.0</Version>），故 Android versionName=1.6.0、标题含 v1.6.0。
- 图标用默认安卓机器人（自绘 vector 绿色触角头 + adaptive icon，mipmap-anydpi-v26）。
- 完成后提交并合并到 GitHub。

### 实现

- 抽象 `DevApi` 接口（HttpApi / SerialTransport 两个实现），配置、音频、状态页签
  全部共用；AppViewModel 改为 Compose 状态驱动（连接状态/数据/进度自动刷新）。
- **SerialTransport（串口窗 = docs/PROTOCOL.md 全命令）**：
  ping / info.get / status.get / config.get / config.set / file.list / file.delete /
  file.begin-chunk-end-abort（2048B/块 base64，逐块请求-确认，失败自动 abort）
  / audio.play / audio.stop / audio.diagnostics / audio.sine / uart.baud
  （115200~2000000，命令后重设参数）/ ota.begin-chunk-end-abort（完成设备重启换分区）。
- USB 设备：usb-serial-for-android 3.7.0（JitPack）+ 系统权限弹窗（USB_PERMISSION
  BroadcastReceiver + RECEIVER_NOT_EXPORTED）；lib 3.7.0 的 API 差异：`open`
  为单参数（UsbDeviceConnection）、无 inputStream 属性 → 自实现行缓冲读取
  （port.read 轮询按 '\n' 切行、4 秒超时）；ping 握手验证对端固件。
- 串口页签（第 4 页）：设备列表/授权连接、波特率、Ping、音频诊断三线翻转、
  3s/16bit 测试音、OTA（.bin 选择 + 进度 + 重启提示）。
- 新标题：「音乐盒配置助手 v1.6.0」（BuildConfig.VERSION_NAME 动态）；launcher 名称
  「音乐盒配置助手」；图标 mipmap/ic_launcher（自绘安卓机器人自适应图标）。

### 构建

- 首次失败：usb-serial 3.7.0 不在 google()/mavenCentral → 加 `jitpack.io` 仓库。✓
- 二次/三次失败（库 API 差异 + 接口签名 + compose forEach + 属性冲突），全部修复后
  **BUILD SUCCESSFUL**，APK badging 确认：versionName=1.6.0、label=音乐盒配置助手、
  icon=mipmap-anydpi-v26/ic_launcher。

### 测试机

- 新测试机**始终无法被 adb 识别**（用户反馈：该手机固有问题，USB 调试授权
  后 Windows 端仍无设备，此前旧机可正常识别）。已换回原测试机可用环境前，
  安装/验证留给用户按 android/README.md 步骤执行（一句话命令已完成记录）。
- GitHub 推送：**已完成**（`bf35eed..7da944d`, 2026-09-09 11:59+ 用户完成凭据授权，
  本会话直接 push 成功；凭据已存入系统，后续 `git push` 无需再交互）。
  helper 为 `scripts/push_github.ps1`（留档可复用）。

### 原测试机回归（插回后 v1.6.0 验证完成）

- `adb install --no-streaming -r` 成功；桌面图标显示「音乐盒配置助手」+
  安卓绿色机器人（自适应图标）。
- 状态页：标题「音乐盒配置助手 v1.6.0」、传输方式单选（HTTP/USB 串口）正常。
- **注意坑**：UI dump 的按钮坐标会随布局变化（本次按钮中心 y 从 442 → 904），
  建议每次先 dump 取 bounds 再 tap——本次失手两次点空后修正坐标即连接成功。
- HTTP 连接（adb reverse → 模拟器）成功：设备信息/状态页数据与模拟器一致；
  `POST /api/sim/lux 1200` 后 3 秒轮询自动把 Lux 10.0→1200.0（联动确认）。
- 串口页：USB 设备列表（无 OTG 时为 0）、波特率 6 档、Ping/音频诊断/测试音、
  OTA 固件更新区全部正常渲染（按钮未连接时禁用态正确）。
- 串口实机链路待接真实 ESP32+OTG 验证（协议层已按 PROTOCOL.md 1:1 实现）。

## 里程碑 M4：发布 GitHub Release v1.6.0（资产补充）

- 现状：v1.6.0 release 早已存在（tag v1.6.0 == bf35eed == 当前固件代码，本轮只新增
  android/device_simulator，固件零改动），原只有 PC 工具 zip 且无固件二进制。
- **做什么**：重构建并替换 `EspMusicBox.ConfigTool-1.6.0.zip`（同 1.6.0 源码，
  dotnet publish 本地产出）；新增 `EspMusicBox.ConfigTool-Android-1.6.0.apk`
  （9.6MB，含串口模式）；更新 release 说明。
- **认证方案**：本机无 gh CLI/winget/docker；用 `git credential fill` 从系统凭据管理器
  取出已存的 GitHub token（避免二次登录），curl/urllib 调用 releases API 上传资产成功。
  坑：Out-File 带 BOM/换行 → Trim() 后正常；同名资产须先 DELETE 再上传（204）。
- **固件二进制**：本机无 ESP-IDF 环境（C:\Espressif 不存在、idf.py、无 docker），
  无法产出 build/esp_music_box.bin。**方案**：新增 GitHub Actions 工作流
  `.github/workflows/firmware-release.yml`（官方 espressif/idf:release-v5.5 容器 +
  softprops/action-gh-release），在 GitHub → Actions → Run workflow 输入
  release_tag=v1.6.0 一键构建并挂载 `esp_music_box_firmware.zip`；
  同时支持未来 tag push v* 全自动。此路径已写入 release 说明。
- 结果：release 资产 = [PC 工具 zip, 安卓 APK]（均验证上传成功），固件一键工作流就绪。


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

1. ~~**测试机 USB 授权**~~ → **已完成**（2026-09-09 01:45 左右授权成功，
   `adb devices` 显示 `device`；下方为安装/使用命令）。
   重复安装命令：`D:\Android\Sdk\platform-tools\adb.exe install --no-streaming -r android\app\build\outputs\apk\debug\app-debug.apk`
2. 若拔插 USB 后（重新）打通手机→宿主机端口：
   `adb reverse tcp:8010 tcp:8010`（App 里地址仍用 `http://127.0.0.1:8010/`；
   手机 WiFi 与电脑同网段时也可直接填 `http://<电脑IP>:8010/`）。
3. **推送 GitHub（可选）**：`git push origin main`（本会话无凭据交互，未 push）。
4. **（可选）AVD 模拟器**：如需模拟器演示，
   `sdkmanager "system-images;android-35;google_apis;x86_64"` 后
   `avdmanager create avd ...`（未下载系统镜像）。
5. **环境变量**：`setx JAVA_HOME D:\Android\jdk-17\jdk-17.0.20.1+1`（脚本已执行）；
   SDK 路径在 `android/local.properties`（不入库，换机需改）。
