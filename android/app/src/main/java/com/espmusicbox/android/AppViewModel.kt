package com.espmusicbox.android

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

enum class Transport { HTTP, USB_SERIAL }

/** 应用级状态容器：HTTP 与 USB 串口（JSON-Lines）两种传输，所有页签共用。 */
class AppViewModel(private val context: Context, private val scope: CoroutineScope) {

    var transport by mutableStateOf(Transport.HTTP)
    var connected by mutableStateOf(false)
    var busy by mutableStateOf(false)

    var info by mutableStateOf<DeviceInfo?>(null)
    var status by mutableStateOf<DeviceStatus?>(null)
    var config by mutableStateOf<AppConfig?>(null)
    var files by mutableStateOf<List<AudioFile>>(emptyList())

    var error by mutableStateOf<String?>(null)
    var notice by mutableStateOf<String?>(null)
    var progressText by mutableStateOf<String?>(null)

    var usbDevices by mutableStateOf<List<UsbDeviceInfo>>(emptyList())
    var usbOpened by mutableStateOf<String?>(null)     // 已打开设备的描述
    var diag by mutableStateOf<Diagnostics?>(null)
    var diagText by mutableStateOf<String?>(null)
    var sineText by mutableStateOf<String?>(null)
    var otaBusy by mutableStateOf(false)

    var baseUrl: String = context.getSharedPreferences("prefs", Context.MODE_PRIVATE)
        .getString("base_url", "http://127.0.0.1:8010/") ?: "http://127.0.0.1:8010/"
        private set
    var serBaud: Int = 115200
        private set

    private var api: DevApi? = null
    private var pollingJob: Job? = null

    data class UsbDeviceInfo(
        val device: android.hardware.usb.UsbDevice,
        val label: String,
        val hasPermission: Boolean,
    )

    fun setBaseUrl(url: String) {
        baseUrl = url.trim()
        context.getSharedPreferences("prefs", Context.MODE_PRIVATE)
            .edit().putString("base_url", baseUrl).apply()
    }

    fun refreshUsbDevices() {
        usbDevices = runCatching {
            UsbSerialUtils.listDevices(context).map { d ->
                UsbDeviceInfo(d, UsbSerialUtils.deviceLabel(d), UsbSerialUtils.hasPermission(context, d))
            }
        }.getOrElse { emptyList() }
    }

    fun requestedPermission(device: android.hardware.usb.UsbDevice, granted: Boolean) {
        if (granted) {
            refreshUsbDevices()
            usbConnect(device)
        } else {
            error = "USB 权限被拒绝（需在系统弹窗点「允许」）"
        }
    }

    /** HTTP 连接 */
    fun connect() {
        scope.launch {
            busy = true
            error = null
            disconnectInternal()
            try {
                val client = HttpApi(baseUrl)
                client.ping()                  // 连通性探测
                api = client
                transport = Transport.HTTP
                connected = true
                refreshAll(client)
                startPolling(client)
            } catch (e: Exception) {
                connected = false
                error = e.message ?: "无法连接"
                api = null
                resetData()
            } finally {
                busy = false
            }
        }
    }

    /** USB 串口连接（设备已获权限时直接打开） */
    fun usbConnect(device: android.hardware.usb.UsbDevice) {
        scope.launch {
            busy = true
            error = null
            disconnectInternal()
            try {
                val serial = SerialTransport(context, device, serBaud).open()
                api = serial
                transport = Transport.USB_SERIAL
                connected = true
                usbOpened = serial.deviceLabel() + " @${serial.baud()} 8N1"
                refreshAll(serial)
                startPolling(serial)
            } catch (e: Exception) {
                connected = false
                error = e.message ?: "串口打开失败"
                usbOpened = null
                api = null
                resetData()
            } finally {
                busy = false
            }
        }
    }

    fun setSerialBaud(baud: Int) {
        if (busy || !connected) return
        scope.launch {
            busy = true
            try {
                api?.setBaud(baud)
                serBaud = baud
                notice = "波特率已切换为 $baud（下次连接默认使用）"
            } catch (e: Exception) {
                error = e.message
            } finally {
                busy = false
            }
        }
    }

    private suspend fun refreshAll(client: DevApi) {
        info = client.info()
        status = client.status()
        config = client.config()
        files = client.files()
    }

    private fun resetData() {
        info = null
        status = null
        config = null
        files = emptyList()
        diag = null
        diagText = null
        sineText = null
    }

    private fun startPolling(client: DevApi) {
        stopPolling()
        pollingJob = scope.launch {
            while (true) {
                delay(3000)
                try {
                    status = client.status()
                } catch (_: Exception) {
                }
            }
        }
    }

    private fun stopPolling() {
        pollingJob?.cancel()
        pollingJob = null
    }

    fun disconnect() {
        scope.launch { disconnectInternal() }
    }

    private suspend fun disconnectInternal() {
        runCatching { api?.close() }
        api = null
        connected = false
        usbOpened = null
        stopPolling()
        resetData()
    }

    fun refreshStatus() {
        scope.launch {
            try {
                status = api?.status()
                error = null
            } catch (e: Exception) {
                error = e.message
            }
        }
    }

    fun refreshConfig() {
        scope.launch {
            try {
                config = api?.config()
                error = null
            } catch (e: Exception) {
                error = e.message
            }
        }
    }

    fun refreshFiles() {
        scope.launch {
            try {
                files = api?.files() ?: emptyList()
                error = null
            } catch (e: Exception) {
                error = e.message
            }
        }
    }

    fun saveConfig(newConfig: AppConfig) {
        scope.launch {
            busy = true
            try {
                api?.putConfig(newConfig)
                config = api?.config()
                notice = "配置已保存"
            } catch (e: Exception) {
                error = e.message
            } finally {
                busy = false
            }
        }
    }

    fun uploadFile(name: String, bytes: ByteArray) {
        scope.launch {
            busy = true
            progressText = "上传 0/${bytes.size}"
            try {
                api?.upload(name, bytes) { sent, total ->
                    progressText = "上传 $sent/$total"
                }
                progressText = "上传完成: $name"
                refreshFiles()
                notice = "已上传 $name"
            } catch (e: Exception) {
                error = e.message
                progressText = null
            } finally {
                busy = false
                scope.launch { delay(1500); progressText = null }
            }
        }
    }

    fun deleteFile(name: String) {
        scope.launch {
            busy = true
            try {
                api?.delete(name)
                refreshFiles()
                refreshConfig()
                refreshStatus()
                notice = "已删除 $name"
            } catch (e: Exception) {
                error = e.message
            } finally {
                busy = false
            }
        }
    }

    fun play(name: String, loop: Boolean) {
        scope.launch {
            busy = true
            try {
                api?.play(name, loop)
                refreshStatus()
                notice = "播放中: $name"
            } catch (e: Exception) {
                error = e.message
            } finally {
                busy = false
            }
        }
    }

    fun stop() {
        scope.launch {
            try {
                api?.stop()
                refreshStatus()
            } catch (e: Exception) {
                error = e.message
            }
        }
    }

    fun setBirthdayFile(name: String) {
        scope.launch {
            busy = true
            try {
                val cfg = api?.config()?.copy(birthdayFile = name) ?: return@launch
                api?.putConfig(cfg)
                config = api?.config()
                notice = "生日歌已设为 $name"
            } catch (e: Exception) {
                error = e.message
            } finally {
                busy = false
            }
        }
    }

    fun runDiagnostics() {
        scope.launch {
            busy = true
            try {
                diag = api?.diagnostics()
                diag = diag
                diagText = null
                val d = diag
                notice = "音频诊断完成（详见串口页）"
            } catch (e: Exception) {
                error = e.message
            } finally {
                busy = false
            }
        }
    }

    fun runSine(seconds: Int = 3, bits: Int = 16) {
        scope.launch {
            busy = true
            try {
                api?.sine(seconds, bits)
                sineText = "已请求测试音 ${seconds}s/${bits}bit，设备将重启播放后继续运行"
                notice = "测试音请求成功"
            } catch (e: Exception) {
                error = e.message
            } finally {
                busy = false
            }
        }
    }

    fun uploadOta(bytes: ByteArray) {
        scope.launch {
            otaBusy = true
            progressText = "OTA 0/${bytes.size}"
            try {
                api?.ota(bytes) { sent, total ->
                    progressText = "OTA $sent/$total"
                }
                progressText = "OTA 完成，设备将自动重启"
                notice = "固件更新完成，设备重启中…"
            } catch (e: Exception) {
                error = e.message
                progressText = null
            } finally {
                otaBusy = false
                scope.launch { delay(3000); progressText = null }
            }
        }
    }

    fun consumeNotice(): String? = notice.also { notice = null }

    fun consumeError(): String? = error.also { error = null }

    fun notifyError(message: String) {
        error = message
    }

    fun toast(message: String) {
        notice = message
    }
}
