# ESP Music Box 通信协议

固件版本 1.2.0。USB CDC、CH340 UART 与 HTTP 使用相同的配置和音频接口。所有字符串均为 UTF-8；文件名只允许 ASCII 字母、数字、点、下划线和连字符，并且扩展名必须为 `.wav` 或 `.mp3`。

## USB CDC / CH340

CH340 使用 UART0 GPIO43(TX)、GPIO44(RX)，参数为 115200-8-N-1；USB CDC 实际不使用波特率。每个请求和响应都是单行 JSON，以 `\n` 结束。主机一次只发送一个请求并等待相同 `id` 的响应。

通用请求：

```json
{"id":1,"cmd":"命令","data":{}}
```

成功响应：

```json
{"id":1,"ok":true,"data":{}}
```

失败响应：

```json
{"id":1,"ok":false,"error":"错误说明"}
```

### 命令

| 命令 | `data` | 返回 `data` |
|---|---|---|
| `ping` | 无 | 字符串 `pong` |
| `info.get` | 无 | 设备信息对象 |
| `status.get` | 无 | 状态对象 |
| `config.get` | 无 | 配置对象 |
| `config.set` | 配置对象（可只含要修改的字段） | 无 |
| `file.list` | 无 | 文件对象数组 |
| `file.begin` | `{"name":"birthday.wav","size":12345}` | 无 |
| `file.chunk` | `{"base64":"..."}` | 无 |
| `file.end` | 无 | 无 |
| `file.abort` | 无 | 无 |
| `file.delete` | `{"name":"birthday.wav"}` | 无 |
| `audio.play` | `{"name":"birthday.wav","loop":true}`；`loop` 可省略，默认 `false` | 无 |
| `audio.stop` | 无 | 无 |
| `audio.diagnostics` | 无 | `{"bclk_toggled":true,"ws_toggled":true,"dout_toggled":true}` |
| `audio.sine` | `{"seconds":5,"bits":16}` | 成功响应后设备重启，开机时播放 1 kHz 测试音 |
| `uart.baud` | `{"baud":115200}`（仅 UART 通道；115200/230400/460800/921600/1500000/2000000） | 响应以新波特率发送 |
| `ota.begin` | `{"size":1200000}` | 无 |
| `ota.chunk` | `{"base64":"..."}` | 无 |
| `ota.end` | 无 | 成功响应后自动重启 |
| `ota.abort` | 无 | 无 |

`audio.play` 的 `loop=true` 表示无限循环所选单曲，直到收到 `audio.stop`、播放另一文件或文件被删除。状态对象中的 `playback_loop` 会在循环期间为 `true`。

`audio.diagnostics` 报告最近一次播放期间 BCLK/WS/DIN 三根信号线是否都观察到了电平翻转，用于排查 I2S 硬件链路。`audio.sine` 保存请求到 NVS 后重启，开机时用裸 `driver/i2s_std.h` 播放 1 kHz 正弦波（16/32-bit 可选），之后正常启动。

串口固件更新依次发送 `ota.begin`、若干 `ota.chunk` 和 `ota.end`，每块原始固件数据最多 2880 字节。固件写入非当前 OTA 分区，并在完整镜像校验通过后切换启动分区；失败时发送 `ota.abort`。

上传时依次发送 `file.begin`、若干 `file.chunk`、`file.end`。每块原始数据最多 2880 字节，再做 Base64 编码；单条 JSON 请求最长 4095 字符。固件先写 `.upload` 临时文件；只有总字节数与声明的 `size` 完全相等才替换目标文件。失败时发送 `file.abort`；若上一次上传被中断，`file.begin` 会自动清理残留会话。设备端 UART0 由独立高优先级任务整行接收后入队处理，客户端可按窗口连续发送多个 `file.chunk`/`ota.chunk` 再统一读取响应以提高吞吐。

配置对象：

```json
{
  "mode":"birthday",
  "wifi_ssid":"MyWiFi",
  "wifi_password":"secret",
  "volume":80,
  "lux_threshold":200,
  "radio_url":"http://example.com/live.mp3",
  "birthday_count":1,
  "birthday_file":"my_song.wav",
  "play_on_boot":false,
  "play_boot_loop":false
}
```

范围：`volume` 为 0～100，`lux_threshold` 为 1～100000，`birthday_count` 为 1～100，`mode` 为 `birthday` 或 `radio`。`birthday_file` 指定生日触发使用的文件；`play_on_boot=true` 时生日模式通电后直接播放，不等待 BH1750；`play_boot_loop=true` 时上电播放改为无限循环（忽略 `birthday_count`）。

设备信息对象：

```json
{
  "device_name":"ESP32-S3 Birthday Music Box",
  "mac":"AA:BB:CC:DD:EE:FF",
  "firmware_version":"1.2.0",
  "flash_size":16777216
}
```

状态对象：

```json
{
  "lux":123.4,
  "mode":"birthday",
  "playback":"stopped",
  "playback_source":"",
  "playback_loop":false,
  "wifi_connected":true,
  "ip_address":"192.168.1.20",
  "littlefs_total":10354688,
  "littlefs_free":9000000,
  "last_error":""
}
```

`playback` 取值为 `stopped`、`buffering`、`playing` 或 `error`。

## HTTP API

默认监听 TCP 80，无鉴权，适合可信局域网。正式产品若处在不可信网络，应在部署层增加隔离或鉴权。

| 方法与路径 | 请求 | 响应 |
|---|---|---|
| `GET /api/info` | 无 | 设备信息 JSON |
| `GET /api/status` | 无 | 状态 JSON |
| `GET /api/config` | 无 | 配置 JSON |
| `PUT /api/config` | 配置 JSON | `{"ok":true}` |
| `GET /api/files` | 无 | 文件数组 JSON |
| `POST /api/files?name=birthday.wav` | 原始音频字节 | `{"ok":true}` |
| `DELETE /api/files?name=birthday.wav` | 无 | `{"ok":true}` |
| `POST /api/play` | `{"name":"birthday.wav","loop":true}` | `{"ok":true}` |
| `POST /api/stop` | 无 | `{"ok":true}` |
| `POST /api/ota` | 预留 | HTTP 501 |

HTTP 失败响应格式为 `{"ok":false,"error":"错误说明"}`。配置或文件变更成功后立即写入 NVS/LittleFS；WiFi 凭据变化会重启 STA 连接。
