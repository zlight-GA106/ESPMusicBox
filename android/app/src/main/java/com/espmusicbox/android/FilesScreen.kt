package com.espmusicbox.android

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Card
import androidx.compose.material3.Checkbox
import androidx.compose.material3.Divider
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FilesScreen(vm: AppViewModel, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    var loop by remember { mutableStateOf(false) }
    var birthdayFile by remember { mutableStateOf("") }
    var uploadHint by remember { mutableStateOf("") }

    val uploadLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri -> uploadHint = uri.toString() }

    LaunchedEffect(uploadHint) {
        val hint = uploadHint
        if (hint.isEmpty()) return@LaunchedEffect
        uploadHint = ""
        val uri = Uri.parse(hint)
        val bytes = withContext(Dispatchers.IO) { readUriBytes(context, uri) }
        if (bytes == null || bytes.isEmpty()) {
            vm.notifyError("无法读取所选文件")
        } else {
            val name = sanitizeUploadName(uri.lastPathSegment ?: "upload_${bytes.size}.wav")
            vm.uploadFile(name, bytes)
        }
    }

    val cfg = vm.config
    if (cfg != null && cfg.birthdayFile != birthdayFile) {
        birthdayFile = cfg.birthdayFile
    }

    Column(modifier = modifier.fillMaxSize().padding(16.dp)) {
        Text("音频文件", style = MaterialTheme.typography.titleLarge)
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilledTonalButton(onClick = { vm.refreshFiles() }, enabled = vm.connected) {
                Icon(Icons.Filled.Refresh, contentDescription = null)
                Spacer(Modifier.width(4.dp))
                Text("刷新")
            }
            OutlinedButton(
                onClick = { uploadLauncher.launch("*/*") },
                enabled = vm.connected
            ) { Text("选择并上传") }
            OutlinedButton(onClick = { vm.stop() }, enabled = vm.connected) {
                Text("停止")
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = loop, onCheckedChange = { loop = it })
                Text("单曲无限循环")
            }
        }
        Spacer(Modifier.height(8.dp))

        if (vm.files.isEmpty()) {
            Text("暂无文件（上传 .wav/.mp3）", style = MaterialTheme.typography.bodyMedium)
        }
        LazyColumn(Modifier.fillMaxWidth()) {
            items(vm.files, key = { it.name }) { file ->
                Card(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
                    Row(
                        Modifier.fillMaxWidth().padding(12.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(Modifier.weight(1f)) {
                            Row {
                                Text(file.name, style = MaterialTheme.typography.titleSmall)
                                if (file.name == birthdayFile) {
                                    Text(" ★生日歌",
                                        color = MaterialTheme.colorScheme.primary,
                                        style = MaterialTheme.typography.bodySmall)
                                }
                            }
                            Text(formatBytes(file.size), style = MaterialTheme.typography.bodySmall)
                        }
                        if (file.name != birthdayFile) {
                            TextButton(onClick = { vm.setBirthdayFile(file.name) },
                                enabled = vm.connected) {
                                Text("设为生日歌")
                            }
                        }
                        TextButton(onClick = { vm.play(file.name, loop) }, enabled = vm.connected) {
                            Text("播放")
                        }
                        TextButton(onClick = { vm.deleteFile(file.name) }, enabled = vm.connected) {
                            Text("删除", color = MaterialTheme.colorScheme.error)
                        }
                    }
                }
                Divider()
            }
        }
    }
}

private fun readUriBytes(context: Context, uri: Uri): ByteArray? = try {
    context.contentResolver.openInputStream(uri)?.use { it.readBytes() }
} catch (_: Exception) {
    null
}

private fun sanitizeUploadName(name: String): String {
    val raw = Uri.decode(name).substringAfterLast('/')
    val keep = raw.filter { it.isLetterOrDigit() || it == '.' || it == '-' || it == '_' }
        .trim('.', '-', '_')
        .ifBlank { "upload.wav" }
    return if (keep.lowercase().endsWith(".wav") || keep.lowercase().endsWith(".mp3")) keep else "$keep.wav"
}
