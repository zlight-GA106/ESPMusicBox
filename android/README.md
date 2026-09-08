# ESP Music Box Android 配置软件

纯 Kotlin + Jetpack Compose Material3 单 Activity 应用，通过设备 HTTP API
（与固件 `main/web_api/web_api.c` 完全一致）配置 ESP32-S3 智能生日音箱。
无第三方 UI 依赖；网络仅用 OkHttp 4 + kotlinx.serialization。

## 功能（三个页签）

1. **状态**：设备地址输入 + 连接/断开；设备名/MAC/固件/Flash、实时 Lux、
   播放状态（停止/缓冲/播放/错误）、播放文件、WiFi/IP、LittleFS 剩余空间、最近错误；
   连接后每 3 秒自动刷新。
2. **配置**：与 Win 配置工具一致 —— 工作模式（生日/网络电台）、触发条件
   （光照度大于/小于 N Lux 时播放生日歌）、死区(Lux)、播放次数、上电播放、
   上电无限循环、WiFi SSID/密码、音量(0-100)、电台 URL；从设备读取/保存。
3. **音频**：文件列表（名称/大小，生日歌文件打 ★ 标记并为单选项）、刷新、
   系统文件选择器上传（自动改为合法 `.wav`/`.mp3` 文件名）、删除、播放/停止、
   「单曲无限循环」复选框。

## 构建

环境要求（本仓库仓库根目录已含一键脚本）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup_android_env.ps1
```

该脚本会安装 Android SDK（`D:\Android\Sdk`）到 cmdline-tools/platform-35/build-tools，
并下载 Temurin JDK 17 到 `D:\Android\jdk-17`（**请勿用 Android Studio 自带的 jbr：
那是 JDK 25，Gradle 8.9 内嵌 Kotlin 无法解析大版本号，会报 `IllegalArgumentException: 25.0.2`**）。

构建（统一命令）：

```powershell
cd android
gradlew.bat assembleDebug
```

产出：`android/app/build/outputs/apk/debug/app-debug.apk`（约 10 MB）。

## 安装到测试机

```powershell
D:\Android\Sdk\platform-tools\adb.exe install --no-streaming -r android/app/build/outputs/apk/debug/app-debug.apk
```

- 注意：部分 MIUI/Redmi ROM 上默认 streaming 安装会挂起，用 `--no-streaming`
  （先 push 再由设备端 pm install）一次成功。
- 若 `adb devices` 显示 `unauthorized`：在手机屏幕弹出的对话框点「允许 USB 调试」后重试。
- 模拟器（AVD）安装：`adb -e install -r ...\app-debug.apk`。

### 电脑端模拟器 + 真机联调（推荐通道）

```powershell
# 1. 电脑启动模拟器
python device_simulator/simulator.py
# 2. 手机 USB 直连电脑后，把手机 127.0.0.1:8010 转发到电脑 8010：
D:\Android\Sdk\platform-tools\adb.exe reverse tcp:8010 tcp:8010
# 3. 手机打开 App，地址填默认 http://127.0.0.1:8010/ 点「连接」即可。
#    （拔插 USB 后需要重新执行 adb reverse）
```

## 连接设备

- 应用"状态"页顶部输入框填 base URL，点「连接」。
- 本机运行模拟器时：Android 模拟器 → `http://10.0.2.2:8010/`；模拟器自身设备模拟器 → `http://127.0.0.1:8010/`。
- 真机 WiFi 同网段：`http://<电脑IP>:8010/`。
- base URL 持久化在 SharedPreferences。

## 触发演示（配合 device_simulator，命令行）

```powershell
python device_simulator/simulator.py
# 状态页设为 http://127.0.0.1:8010/ 连接

# 固定低光 → 确认 stopped；再设高光超过阈值（默认 above 300Lux）→ 秒内播放生日歌
curl http://127.0.0.1:8010/api/sim/lux -H "Content-Type: application/json" -d "{\"lux\":10}"
curl http://127.0.0.1:8010/api/sim/lux -H "Content-Type: application/json" -d "{\"lux\":1200}"
curl http://127.0.0.1:8010/api/status      # playback="playing" playback_source="birthday.wav"
```

## 工程结构

```text
android/
  settings.gradle.kts / build.gradle.kts / gradle.properties / local.properties(SDK 路径，不入库)
  gradlew.bat, gradle/wrapper/             Gradle 8.9 wrapper
  app/
    build.gradle.kts                       AGP 8.7.3 / Kotlin 2.0.21 / Compose / serialization
    src/main/java/com/espmusicbox/android/
      MainActivity.kt                      Scaffold + 底部导航
      Models.kt                            API 数据类（snake_case 字段名与设备一致）
      Api.kt                               OkHttp 客户端（GET/PUT/POST/DELETE /api/*）
      AppViewModel.kt                      状态容器 + 3 秒轮询 + 会话 baseUrl
      StatusScreen.kt                      状态页
      ConfigScreen.kt                      配置页
      FilesScreen.kt                       音频页（上传/删除/播放/设为生日歌）
```
