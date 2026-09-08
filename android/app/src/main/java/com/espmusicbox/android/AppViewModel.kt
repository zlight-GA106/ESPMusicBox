package com.espmusicbox.android

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/** 应用级状态容器：持有当前连接、设备信息、状态、配置、文件列表。 */
class AppViewModel(private val context: Context, private val scope: CoroutineScope) {

    var baseUrl: String = context.getSharedPreferences("prefs", Context.MODE_PRIVATE)
        .getString("base_url", "http://127.0.0.1:8010/") ?: "http://127.0.0.1:8010/"
        private set

    var connected: Boolean = false
        private set
    var busy: Boolean = false
        private set

    var info: DeviceInfo? = null
        private set
    var status: DeviceStatus? = null
        private set
    var config: AppConfig? = null
        private set
    var files: List<AudioFile> = emptyList()
        private set

    var error: String? = null
        private set
    var notice: String? = null
        private set

    private var api: DeviceApi? = null
    private var pollingJob: kotlinx.coroutines.Job? = null

    fun setBaseUrl(url: String) {
        baseUrl = url.trim()
        context.getSharedPreferences("prefs", Context.MODE_PRIVATE)
            .edit().putString("base_url", baseUrl).apply()
    }

    fun setConnected(value: Boolean) {
        connected = value
        if (!value) stopPolling()
    }

    fun connect() {
        scope.launch {
            busy = true
            error = null
            try {
                val client = DeviceApi(baseUrl)
                client.getInfo()          // 连通性探测
                api = client
                connected = true
                info = client.getInfo()
                refreshStatus()
                refreshConfig()
                refreshFiles()
                startPolling(client)
            } catch (e: Exception) {
                connected = false
                error = e.message ?: "无法连接"
                api = null
                info = null
                status = null
                config = null
                files = emptyList()
            } finally {
                busy = false
            }
        }
    }

    private fun startPolling(client: DeviceApi) {
        stopPolling()
        pollingJob = scope.launch {
            while (true) {
                delay(3000)
                try {
                    status = client.getStatus()
                } catch (_: Exception) {
                }
            }
        }
    }

    private fun stopPolling() {
        pollingJob?.cancel()
        pollingJob = null
    }

    fun refreshStatus() {
        scope.launch {
            try {
                status = api?.getStatus()
                error = null
            } catch (e: Exception) {
                error = e.message
            }
        }
    }

    fun refreshConfig() {
        scope.launch {
            try {
                config = api?.getConfig()
                error = null
            } catch (e: Exception) {
                error = e.message
            }
        }
    }

    fun refreshFiles() {
        scope.launch {
            try {
                files = api?.getFiles() ?: emptyList()
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
                config = api?.getConfig()
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
            try {
                api?.uploadFile(name, bytes)
                refreshFiles()
                notice = "已上传 $name"
            } catch (e: Exception) {
                error = e.message
            } finally {
                busy = false
            }
        }
    }

    fun deleteFile(name: String) {
        scope.launch {
            busy = true
            try {
                api?.deleteFile(name)
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
                val cfg = api?.getConfig()?.copy(birthdayFile = name) ?: return@launch
                api?.putConfig(cfg)
                config = api?.getConfig()
                notice = "生日歌已设为 $name"
            } catch (e: Exception) {
                error = e.message
            } finally {
                busy = false
            }
        }
    }

    fun consumeNotice(): String? = notice.also { notice = null }

    fun consumeError(): String? = error.also { error = null }

    fun notifyError(message: String) {
        error = message
    }
}
