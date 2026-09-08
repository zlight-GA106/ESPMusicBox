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
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ElevatedButton
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConfigScreen(vm: AppViewModel, modifier: Modifier = Modifier) {
    var config by remember { mutableStateOf<AppConfig?>(null) }
    var editing by remember { mutableStateOf<AppConfig?>(null) }
    var luminance by remember { mutableStateOf("300") }
    var deadZone by remember { mutableStateOf("50") }
    var count by remember { mutableStateOf("1") }

    LaunchedEffect(vm.config) {
        vm.config?.let {
            if (config == null || config != it) {
                config = it
                editing = it
                luminance = formatNum(it.triggerLux)
                deadZone = formatNum(it.deadZoneLux)
                count = it.birthdayCount.toString()
            }
        }
    }

    Column(
        modifier = modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text("设备配置", style = MaterialTheme.typography.titleLarge)

        Row {
            OutlinedButton(onClick = { vm.refreshConfig() }, enabled = vm.connected) {
                Text("从设备读取")
            }
            Spacer(Modifier.width(8.dp))
            ElevatedButton(
                onClick = {
                    val base = editing ?: return@ElevatedButton
                    val newCfg = base.copy(
                        triggerLux = luminance.toDoubleOrNull() ?: base.triggerLux,
                        deadZoneLux = deadZone.toDoubleOrNull() ?: base.deadZoneLux,
                        birthdayCount = count.toIntOrNull() ?: base.birthdayCount,
                        volume = base.volume
                    )
                    vm.saveConfig(newCfg)
                },
                enabled = vm.connected
            ) { Text("保存到设备") }
        }

        editing?.let { cfg ->
            Text("工作模式", style = MaterialTheme.typography.titleMedium)
            Row {
                RadioButton(selected = cfg.mode == "birthday",
                    onClick = { editing = cfg.copy(mode = "birthday") })
                Text("生日模式", Modifier.padding(top = 8.dp))
                Spacer(Modifier.width(12.dp))
                RadioButton(selected = cfg.mode == "radio",
                    onClick = { editing = cfg.copy(mode = "radio") })
                Text("网络电台", Modifier.padding(top = 8.dp))
            }

            Text("触发条件", style = MaterialTheme.typography.titleMedium)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("光照度")
                Spacer(Modifier.width(4.dp))
                RadioButton(selected = cfg.triggerDirection == "above",
                    onClick = { editing = cfg.copy(triggerDirection = "above") })
                Text("大于")
                Spacer(Modifier.width(12.dp))
                RadioButton(selected = cfg.triggerDirection == "below",
                    onClick = { editing = cfg.copy(triggerDirection = "below") })
                Text("小于")
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = luminance,
                    onValueChange = { luminance = it },
                    label = { Text("阈值 (Lux)") },
                    singleLine = true,
                    modifier = Modifier.width(150.dp)
                )
                Text(" Lux 时播放生日歌", Modifier.padding(start = 8.dp))
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = deadZone,
                    onValueChange = { deadZone = it },
                    label = { Text("死区 (Lux)") },
                    singleLine = true,
                    modifier = Modifier.width(150.dp)
                )
                Text("  重新武装须回到阈值±死区", Modifier.padding(start = 8.dp))
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = count,
                    onValueChange = { count = it },
                    label = { Text("播放次数 (1-100)") },
                    singleLine = true,
                    modifier = Modifier.width(150.dp)
                )
            }

            Text("上电播放", style = MaterialTheme.typography.titleMedium)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = cfg.playOnBoot,
                    onCheckedChange = { editing = cfg.copy(playOnBoot = it) })
                Text("通电后直接播放（忽略 BH1750）")
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = cfg.playBootLoop,
                    onCheckedChange = { editing = cfg.copy(playBootLoop = it) })
                Text("上电播放无限循环")
            }

            Text("WiFi / 音量 / 电台", style = MaterialTheme.typography.titleMedium)
            OutlinedTextField(
                value = cfg.wifiSsid,
                onValueChange = { editing = cfg.copy(wifiSsid = it) },
                label = { Text("WiFi SSID") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )
            OutlinedTextField(
                value = cfg.wifiPassword,
                onValueChange = { editing = cfg.copy(wifiPassword = it) },
                label = { Text("WiFi 密码") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )
            Row {
                Text("音量: ${cfg.volume}")
                Slider(
                    value = cfg.volume.toFloat(),
                    onValueChange = { editing = cfg.copy(volume = it.toInt()) },
                    valueRange = 0f..100f,
                    modifier = Modifier.weight(1f).padding(start = 8.dp)
                )
            }
            OutlinedTextField(
                value = cfg.radioUrl,
                onValueChange = { editing = cfg.copy(radioUrl = it) },
                label = { Text("电台 URL（模式=网络电台时播放）") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )

            InfoRow("生日歌文件", cfg.birthdayFile.ifEmpty { "(未设置)" })
            Text("提示：在「音频」页勾选任意文件右上角按钮「设为生日歌」，"
                    + "播放次数/阈值等保存后立即生效。",
                style = MaterialTheme.typography.bodySmall)
        } ?: Text("点击「从设备读取」加载配置", style = MaterialTheme.typography.bodyMedium)
    }
}

private fun formatNum(v: Double): String =
    if (v == v.toLong().toDouble()) v.toLong().toString() else v.toString()
