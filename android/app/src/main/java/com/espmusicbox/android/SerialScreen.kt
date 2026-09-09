package com.espmusicbox.android

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
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
import androidx.compose.material3.ElevatedButton
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun SerialScreen(vm: AppViewModel, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    // USB 权限结果：请求 → 系统弹窗 → 广播回调
    DisposableEffect(Unit) {
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(ctx: Context, intent: Intent) {
                if (intent.action != USB_PERMISSION_ACTION) return
                val device = intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE)
                    ?: return
                val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                vm.requestedPermission(device, granted)
            }
        }
        val filter = IntentFilter(USB_PERMISSION_ACTION)
        if (android.os.Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            context.registerReceiver(receiver, filter)
        }
        onDispose { context.unregisterReceiver(receiver) }
    }

    var otaBytes by remember { mutableStateOf<ByteArray?>(null) }
    var otaName by remember { mutableStateOf("") }
    val otaLauncher = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri != null) {
            otaName = uri.lastPathSegment ?: "firmware.bin"
            scope.launch {
                otaBytes = withContext(Dispatchers.IO) {
                    runCatching { context.contentResolver.openInputStream(uri)?.use { it.readBytes() } }.getOrNull()
                }
                if (otaBytes == null) vm.notifyError("无法读取固件文件")
            }
        }
    }

    LaunchedEffect(vm.transport) { vm.refreshUsbDevices() }

    Column(
        modifier = modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text("串口设置", style = MaterialTheme.typography.titleLarge)
        Text("USB 直连（OTG）：手机为 USB Host，连接固件的原生 USB CDC 或 CH340 设备。"
            + "要求手机支持 USB OTG 且开启 OTG 供电。", style = MaterialTheme.typography.bodySmall)

        when (vm.transport) {
            Transport.USB_SERIAL -> {
                Text("当前传输：USB 串口（JSON-Lines 协议，命令与 docs/PROTOCOL.md 一致）",
                    color = MaterialTheme.colorScheme.primary)
                InfoRow("已连接", vm.usbOpened ?: "-")
                OutlinedButton(onClick = { vm.disconnect() }) { Text("断开") }
            }
            else -> {
                Text("当前传输：HTTP（状态页点「连接」或下方切换）",
                    style = MaterialTheme.typography.bodySmall)
                OutlinedButton(onClick = { vm.refreshUsbDevices() }) { Text("刷新设备列表") }
            }
        }

        Text("USB 串口设备（${vm.usbDevices.size}）", style = MaterialTheme.typography.titleMedium)
        vm.usbDevices.forEach { dev ->
            Card(Modifier.fillMaxWidth()) {
                Row(
                    Modifier.fillMaxWidth().padding(10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Column(Modifier.weight(1f)) {
                        Text(dev.label, style = MaterialTheme.typography.bodyMedium)
                        Text(if (dev.hasPermission) "已授权" else "未授权",
                            style = MaterialTheme.typography.bodySmall)
                    }
                    if (vm.connected && vm.transport == Transport.USB_SERIAL) {
                        OutlinedButton(onClick = {}) { Text("已连接") }
                    } else if (dev.hasPermission) {
                        Button(onClick = { vm.usbConnect(dev.device) }) { Text("连接") }
                    } else {
                        Button(onClick = {
                            UsbSerialUtils.requestPermission(context, dev.device)
                        }) { Text("授权并连接") }
                    }
                }
            }
        }
        if (vm.usbDevices.isEmpty()) {
            Text("没有发现串口设备：请用 OTG 转接线连接 ESP32-S3（原生 USB CDC 口，"
                + "或 CH340 UART0），然后在系统弹出的「允许 USB 调试协议」类窗口确认授权。",
                style = MaterialTheme.typography.bodySmall)
        }

        Text("波特率（仅 CH340 UART 生效；USB CDC 忽略）", style = MaterialTheme.typography.titleMedium)
        for (row in SerialTransport.BAUD_OPTIONS.chunked(3)) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for (b in row) {
                    OutlinedButton(
                        onClick = { vm.setSerialBaud(b) },
                        enabled = vm.connected && vm.transport == Transport.USB_SERIAL,
                        modifier = Modifier.weight(1f).fillMaxWidth()
                    ) { Text("$b") }
                }
            }
        }

        Text("连接测试与音频诊断", style = MaterialTheme.typography.titleMedium)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { vm.refreshStatus() },
                enabled = vm.connected) { Text("Ping/刷新") }
            OutlinedButton(onClick = { vm.runDiagnostics() },
                enabled = vm.connected) { Text("音频诊断(BCLK/WS/DIN)") }
            OutlinedButton(onClick = { vm.runSine(3, 16) },
                enabled = vm.connected) { Text("测试音 3s/16bit") }
        }
        vm.diag?.let { d ->
            Text("BCLK: " + if (d.bclkToggled) "翻转 ✓" else "未翻转 ✗",
                style = MaterialTheme.typography.bodyMedium)
            Text("WS:   " + if (d.wsToggled) "翻转 ✓" else "未翻转 ✗",
                style = MaterialTheme.typography.bodyMedium)
            Text("DIN:  " + if (d.doutToggled) "翻转 ✓" else "未翻转 ✗",
                style = MaterialTheme.typography.bodyMedium)
        }
        vm.sineText?.let { Text(it, style = MaterialTheme.typography.bodyMedium) }

        Text("固件更新（OTA）", style = MaterialTheme.typography.titleMedium)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedButton(onClick = { otaLauncher.launch("*/*") },
                enabled = vm.connected && !vm.otaBusy) { Text("选择固件 (.bin)") }
            ElevatedButton(onClick = {
                val bytes = otaBytes
                if (bytes != null) vm.uploadOta(bytes) else vm.notifyError("请先选择固件文件")
            }, enabled = vm.connected && otaBytes != null && !vm.otaBusy) { Text("开始更新") }
        }
        if (otaBytes != null) {
            Text("固件：$otaName（${formatBytes(otaBytes!!.size.toLong())}）",
                style = MaterialTheme.typography.bodySmall)
        }
        vm.progressText?.let { Text(it, color = MaterialTheme.colorScheme.primary) }
        Text("OTA 成功后设备立即重启并切到新分区；开始后请勿拔掉 USB。",
            style = MaterialTheme.typography.bodySmall)
    }
}
