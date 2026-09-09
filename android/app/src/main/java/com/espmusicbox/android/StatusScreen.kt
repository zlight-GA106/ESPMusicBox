package com.espmusicbox.android

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun StatusScreen(vm: AppViewModel, modifier: Modifier = Modifier) {
    var urlInput by remember { mutableStateOf(vm.baseUrl) }

    Column(
        modifier = modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("音乐盒配置助手", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.width(6.dp))
            Text("v${BuildConfig.VERSION_NAME}",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Text("传输方式", style = MaterialTheme.typography.titleMedium)
        Row(verticalAlignment = Alignment.CenterVertically) {
            RadioButton(selected = vm.transport == Transport.HTTP,
                onClick = {
                    if (vm.transport != Transport.HTTP && vm.connected) vm.disconnect()
                    vm.transport = Transport.HTTP
                })
            Text("HTTP（设备地址或模拟器）")
            Spacer(Modifier.width(12.dp))
            RadioButton(selected = vm.transport == Transport.USB_SERIAL,
                onClick = {
                    if (vm.transport != Transport.USB_SERIAL && vm.connected) vm.disconnect()
                    vm.transport = Transport.USB_SERIAL
                })
            Text("USB 串口")
        }

        Text("设备连接", style = MaterialTheme.typography.titleMedium)
        if (vm.transport == Transport.HTTP) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = urlInput,
                    onValueChange = { urlInput = it },
                    label = { Text("设备地址 (HTTP)") },
                    singleLine = true,
                    modifier = Modifier.weight(1f)
                )
                Spacer(Modifier.width(8.dp))
                if (!vm.connected) {
                    Button(onClick = {
                        vm.setBaseUrl(urlInput)
                        vm.connect()
                    }) { Text("连接") }
                } else {
                    OutlinedButton(onClick = { vm.disconnect() }) { Text("断开") }
                }
            }
            if (vm.connected) {
                Text("已连接: ${vm.baseUrl}", style = MaterialTheme.typography.bodySmall)
            }
        } else {
            Text("串口连接请在「串口」页选择设备并授权（USB OTG）。",
                style = MaterialTheme.typography.bodyMedium)
        }
        if (vm.busy) {
            Text("请求中…", style = MaterialTheme.typography.bodySmall)
        }
        vm.error?.let {
            Text("错误: $it", color = MaterialTheme.colorScheme.error)
        }

        val info = vm.info
        if (info != null) {
            Text("设备信息", style = MaterialTheme.typography.titleMedium)
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    InfoRow("名称", info.deviceName)
                    InfoRow("MAC", info.mac)
                    InfoRow("固件版本", info.firmwareVersion)
                    InfoRow("Flash 大小", formatBytes(info.flashSize))
                }
            }
        }

        val status = vm.status
        if (status != null) {
            Text("实时状态", style = MaterialTheme.typography.titleMedium)
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    InfoRow("Lux(光照)", "%.1f".format(status.lux))
                    InfoRow("工作模式", if (status.mode == "birthday") "生日模式" else "网络电台")
                    InfoRow("播放状态", playbackText(status.playback))
                    InfoRow("播放文件", status.playbackSource.ifEmpty { "-" })
                    InfoRow("循环播放", if (status.playbackLoop) "开" else "关")
                    InfoRow("WiFi", if (status.wifiConnected) "已连接" else "未连接")
                    InfoRow("IP 地址", status.ipAddress.ifEmpty { "-" })
                    InfoRow("LittleFS 剩余", "${formatBytes(status.littlefsFree)} / ${formatBytes(status.littlefsTotal)}")
                    InfoRow("最近错误", status.lastError.ifEmpty { "无" })
                }
            }
            Row {
                OutlinedButton(onClick = { vm.refreshStatus() }, enabled = vm.connected) {
                    Text("刷新状态")
                }
            }
        } else if (vm.connected) {
            Text("还没有状态数据，请刷新", style = MaterialTheme.typography.bodyMedium)
        } else {
            Text("输入设备地址后点击「连接」。（Android 模拟器访问宿主机用 http://10.0.2.2:8010/，"
                    + "模拟器内运行设备模拟器时用 http://127.0.0.1:8010/）",
                style = MaterialTheme.typography.bodyMedium)
        }
    }
}

fun playbackText(playback: String): String = when (playback) {
    "stopped" -> "已停止"
    "buffering" -> "缓冲中"
    "playing" -> "播放中"
    "error" -> "错误"
    else -> playback
}

fun formatBytes(bytes: Long): String = when {
    bytes >= 1024 * 1024 -> "%.1f MB".format(bytes / 1024.0 / 1024.0)
    bytes >= 1024 -> "%.1f KB".format(bytes / 1024.0)
    else -> "$bytes B"
}

@Composable
fun InfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value)
    }
}
