# ESP32-S3 智能生日音箱

基于 ESP-IDF 5.3～5.5 和 C 语言的 ESP32-S3-N16R8 音箱工程，不使用 Arduino。配套的 Windows 配置工具位于 `pc_tool/EspMusicBox.ConfigTool`。

## 已实现

- BH1750（GPIO8/9）每 500 ms 采样，按 Lux 变化量触发生日歌
- MAX98357A（BCLK=GPIO4、WS=GPIO5、DIN=GPIO6）I2S DMA 输出
- LittleFS 中的 PCM 8/16/24/32-bit、32-bit Float WAV、本地 MP3 和 HTTP/HTTPS MP3 流
- PSRAM 音频缓冲、0～100 软件音量、播放/停止、单曲无限循环和电台自动重连
- NVS 配置、可选生日歌文件、通电直接播放（可选无限循环），同时镜像为 `/littlefs/config/config.json`
- USB CDC、CH340 UART0 JSON Lines 配置通道和 WiFi HTTP API
- 上传、删除、列表和试听 LittleFS 音频
- 双 OTA 应用分区，支持通过 USB CDC 或 CH340 更新应用固件

## 硬件连接

| ESP32-S3 | 外设 |
|---|---|
| GPIO8 | BH1750 SDA |
| GPIO9 | BH1750 SCL |
| GPIO4 | MAX98357A BCLK |
| GPIO5 | MAX98357A LRC/WS |
| GPIO6 | MAX98357A DIN |
| GPIO19 | USB D-（使用原生 USB CDC 时） |
| GPIO20 | USB D+（使用原生 USB CDC 时） |
| GPIO43 | CH340 RX（ESP32 UART0 TX） |
| GPIO44 | CH340 TX（ESP32 UART0 RX） |

MAX98357A 的 `SPK+`、`SPK-` 直接接喇叭两端；`SPK-` 不能接 GND。BH1750 模块若没有板载上拉，请给 SDA/SCL 增加约 4.7 kΩ 上拉电阻。使用带两个 USB 口的开发板时，CDC 应连接标有 `USB`/`OTG` 的原生 USB 口。

## 构建固件

先安装并进入 ESP-IDF 5.3～5.5 命令行：

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

LittleFS、Helix MP3、esp_tinyusb 与 TinyUSB 的固定版本源码已放在 `components/`，无需构建时再从组件仓库下载；LittleFS 的镜像生成工具若本机没有，首次构建仍可能通过 Python 安装。`flash` 会同时写入应用和由 `littlefs/` 生成的文件系统镜像。

生日歌可在上位机音频列表中勾选任意已上传的 WAV/MP3。WAV 支持单/双声道、PCM 8/16/24/32 bit、32-bit Float、WAVE_FORMAT_EXTENSIBLE 和 8～96 kHz；实际使用推荐 16-bit PCM。电台 URL 必须直接返回 MP3 字节流，不支持 M3U/PLS/HLS 播放列表或 AAC。

BH1750 未连接或暂时掉线不会阻止系统启动；`sensor_task` 每 5 秒重试一次，恢复连接后自动继续采样。

16 MB 分区布局：

| 分区 | 大小 | 用途 |
|---|---:|---|
| NVS | 20 KB | 配置、WiFi、PHY 数据 |
| OTA data | 8 KB | OTA 启动选择 |
| ota_0 | 3 MB | 当前/升级固件 |
| ota_1 | 3 MB | 备用固件 |
| littlefs | 9.875 MB | 音频和配置镜像 |
| coredump | 64 KB | 崩溃信息 |

## 构建 Windows 配置工具

需要 .NET 8 SDK 或更高版本：

```powershell
dotnet build pc_tool/EspMusicBox.ConfigTool/EspMusicBox.ConfigTool.csproj
dotnet run --project pc_tool/EspMusicBox.ConfigTool/EspMusicBox.ConfigTool.csproj
```

本机已生成的 1.4.0 Release 程序位于 `dist/EspMusicBox.ConfigTool-1.4.0/EspMusicBox.ConfigTool.exe`。

串口连接可选择原生 USB CDC 或板载 CH340；设备已联网时也可输入 `http://设备IP/` 使用 HTTP。固件更新页支持 CDC/CH340 串口 OTA。通信细节见 [docs/PROTOCOL.md](docs/PROTOCOL.md)。

## 生日触发语义

启动后首个有效读数成为亮度基线。未触发时基线以 5% 的速度缓慢跟随环境；当前亮度与基线的绝对差达到 `lux_threshold` 后播放已勾选的生日歌指定次数。亮度回到阈值的 40% 以内才会重新武装，因此一次持续变化只触发一次。启用“通电后直接播放”时跳过首次 BH1750 触发等待。

## 工程结构

```text
main/
  app_main.c                 启动和任务创建
  audio/                     I2S、WAV、MP3、播放任务
  sensor/bh1750.c            BH1750 与 sensor_task
  filesystem/littlefs.c      挂载、文件操作与 storage_task
  config/nvs_config.c        NVS 配置
  wifi/wifi_manager.c        WiFi 维护与 wifi_task
  web_api/web_api.c          HTTP API
  usb_serial/usb_serial.c    USB CDC、CH340、串口 OTA 与 config_task
littlefs/                    首次烧录的文件系统内容
pc_tool/                     WinForms 配置工具
docs/PROTOCOL.md             通信协议
```
