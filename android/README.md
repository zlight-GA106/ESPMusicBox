# ESP Music Box 音乐盒配置助手（Android）

纯 Kotlin + Jetpack Compose Material3 单 Activity 应用。名称「音乐盒配置助手」，
版本号跟随电脑端 pc_tool（当前 1.6.0），图标为默认安卓机器人。
支持两种传输并共用同一套配置页面：

- **HTTP**：设备 HTTP API（`main/web_api/web_api.c`，端口 80）
- **USB 串口**：USB OTG 直连固件（USB CDC 原生口 / CH340 UART0），JSON-Lines 协议
  （`docs/PROTOCOL.md` 全部命令 1:1 移植）

网络仅用 OkHttp 4 + kotlinx.serialization；串口用 `usb-serial-for-android`（第三方库
非 UI 库，UI 零依赖）。

## 四个页签

1. **状态**：标题「音乐盒配置助手 v1.6.0」+ 传输方式选择（HTTP / USB 串口）；
   地址输入/连接断开、设备名/MAC/固件/Flash、实时 Lux、播放状态、WiFi/IP、
   LittleFS 剩余、最近错误；连接后每 3 秒自动刷新。
2. **配置**：工作模式（生日/电台）、触发条件（大于/小于 N Lux）、死区、播放次数、
   上电播放、上电无限循环、WiFi SSID/密码、音量 0-100、电台 URL；读取/保存。
   串口方式下等价执行 `config.get` / `config.set`。
3. **音频**：列表（★生日歌标记）、刷新、上传（HTTP octet-stream / 串口
   `file.begin→file.chunk(2048B块,base64)→file.end`）、删除（`file.delete`）、
   播放/停止、单曲无限循环。
4. **串口**：USB 设备列表 + 权限授权（系统弹窗）+ 连接/断开；波特率
   （115200~2000000，`uart.baud`；仅 CH340 生效）；Ping；音频诊断
   （`audio.diagnostics` → BCLK/WS/DIN 翻转 ✓/✗）；测试音
   （`audio.sine` 3s/16bit，设备会重启）；固件 OTA（`ota.begin→chunk→end`，
   完成后设备自动重启换分区）。

## 串口命令对照表（docs/PROTOCOL.md，全部实现）

| 命令 | 用途 | 串口页 位置 |
|---|---|---|
| `ping` | 握手探测 | 自动（连接时） |
| `info.get` / `status.get` / `config.get` | 信息/状态/配置 | 状态页、配置页 |
| `config.set` | 写配置（部分字段） | 配置页「保存」 |
| `file.list` / `file.delete` / `file.begin` / `file.chunk` / `file.end` / `file.abort` | 音频管理 | 音频页 |
| `audio.play` / `audio.stop` | 播放/停止 | 音频页 |
| `audio.diagnostics` | BCLK/WS/DIN 电平翻转诊断 | 串口页 |
| `audio.sine` | 1kHz 测试音后重启 | 串口页 |
| `uart.baud` | 切换 UART 波特率 | 串口页 |
| `ota.begin` / `ota.chunk` / `ota.end` / `ota.abort` | 固件更新（自动重启换分区） | 串口页 |

每块原始 2048B（协议上限 2880B）；取逐块请求-确认模式（实现简单且可靠，与
pc_tool 可连续发送块的窗口模式语义一致）。

## 构建

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup_android_env.ps1   # 一次性环境
cd android
gradlew.bat assembleDebug
```

产出：`android/app/build/outputs/apk/debug/app-debug.apk`（约 10 MB）。

## 安装到测试机

```powershell
D:\Android\Sdk\platform-tools\adb.exe install --no-streaming -r android/app/build/outputs/apk/debug/app-debug.apk
```

- 部分 MIUI/Redmi ROM 的 streaming 安装会挂起，用 `--no-streaming` 一次成功。
- `adb devices` 显示 `unauthorized` 时在手机弹窗点「允许 USB 调试」。
- 拔插 USB 后恢复手机→宿主机映射：`adb reverse tcp:8010 tcp:8010`。

## 串口模式使用（真机 + ESP32-S3）

1. 手机用 OTG 转接线接 ESP32-S3 的**原生 USB 口**或 CH340 模块（UART0 交叉：
   ESP32 RX ← CH340 TX）。
2. 打开 App「串口」页 → 「刷新设备列表」→ 点「授权并连接」（系统弹窗点允许）。
3. 连接成功会发 `ping` 握手，随后「状态/配置/音频」页签全部走串口协议。
4. CH340 模式下切波特率通过 `uart.baud` 生效；USB CDC 忽略波特率（115200-8-N-1 语义）。
5. 固件更新在「串口」页选择 .bin 后开始；开始后不要拔线，设备完成会重启。

## 触发演示（HTTP 模拟器，验证联动）

```powershell
python device_simulator/simulator.py
curl.exe -X POST http://127.0.0.1:8010/api/sim/lux -H "Content-Type: application/json" -d '{\"lux\":10}'
curl.exe -X POST http://127.0.0.1:8010/api/sim/lux -H "Content-Type: application/json" -d '{\"lux\":1200}'
curl.exe http://127.0.0.1:8010/api/status   # playback="playing" playback_source="birthday.wav"
```
Android 模拟器内访问宿主机用 `http://10.0.2.2:8010/`；真机连模拟器用 `adb reverse`。

## 工程结构

```text
android/
  settings.gradle.kts / build.gradle.kts / gradle.properties / local.properties(SDK 路径，不入库)
  gradlew.bat, gradle/wrapper/             Gradle 8.9 wrapper
  app/
    build.gradle.kts                       AGP 8.7.3 / Kotlin 2.0.21 / usb-serial 3.7.0
    src/main/java/com/espmusicbox/android/
      MainActivity.kt                      Scaffold + 4 页签底部导航
      Models.kt                            API 数据类（snake_case 与设备一致）
      Api.kt                               DevApi 接口 + HttpApi（HTTP 实现）
      SerialClient.kt                      USB 发现/授权 + SerialTransport（JSON-Lines 全命令）
      AppViewModel.kt                      状态容器 + 传输抽象 + 3 秒轮询 + OTA/诊断/进度
      StatusScreen.kt                      状态页（含传输方式选择）
      ConfigScreen.kt                      配置页（HTTP/serial 共用）
      FilesScreen.kt                       音频页（上传/删除/播放/生日歌标记）
      SerialScreen.kt                      串口页（设备/波特率/诊断/测试音/OTA）
    src/main/res/mipmap-anydpi-v26/ic_launcher.xml   默认安卓机器人自适应图标
