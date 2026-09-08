package com.espmusicbox.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.List
import androidx.compose.material.icons.filled.Settings
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            val context = LocalContext.current.applicationContext
            val scope = rememberCoroutineScope()
            val vm = remember {
                AppViewModel(context, scope)
            }
            AppRoot(vm)
        }
    }
}

val currentTab = MutableStateFlow(0)

@Composable
fun AppRoot(vm: AppViewModel) {
    val snackbar = remember { SnackbarHostState() }

    LaunchedEffect(vm) {
        while (true) {
            vm.consumeNotice()?.let { snackbar.showSnackbar(it) }
            vm.consumeError()?.let { snackbar.showSnackbar(it) }
            delay(500)
        }
    }

    val tab by currentTab.collectAsState()

    Scaffold(
        snackbarHost = { SnackbarHost(snackbar) },
        bottomBar = {
            NavigationBar {
                NavigationBarItem(
                    selected = tab == 0,
                    onClick = { currentTab.value = 0 },
                    icon = { Icon(Icons.Filled.Home, contentDescription = null) },
                    label = { Text("状态") }
                )
                NavigationBarItem(
                    selected = tab == 1,
                    onClick = { currentTab.value = 1 },
                    icon = { Icon(Icons.Filled.Settings, contentDescription = null) },
                    label = { Text("配置") }
                )
                NavigationBarItem(
                    selected = tab == 2,
                    onClick = { currentTab.value = 2 },
                    icon = { Icon(Icons.Filled.List, contentDescription = null) },
                    label = { Text("音频") }
                )
            }
        }
    ) { padding ->
        val mod = Modifier.padding(padding)
        when (tab) {
            0 -> StatusScreen(vm, mod)
            1 -> ConfigScreen(vm, mod)
            else -> FilesScreen(vm, mod)
        }
    }
}
