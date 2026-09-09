package com.espmusicbox.android

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class DeviceInfo(
    @SerialName("device_name") val deviceName: String = "",
    val mac: String = "",
    @SerialName("firmware_version") val firmwareVersion: String = "",
    @SerialName("flash_size") val flashSize: Long = 0,
)

@Serializable
data class DeviceStatus(
    val lux: Double = 0.0,
    val mode: String = "birthday",
    val playback: String = "stopped",
    @SerialName("playback_source") val playbackSource: String = "",
    @SerialName("playback_loop") val playbackLoop: Boolean = false,
    @SerialName("wifi_connected") val wifiConnected: Boolean = false,
    @SerialName("ip_address") val ipAddress: String = "",
    @SerialName("littlefs_total") val littlefsTotal: Long = 0,
    @SerialName("littlefs_free") val littlefsFree: Long = 0,
    @SerialName("last_error") val lastError: String = "",
)

@Serializable
data class AppConfig(
    val mode: String = "birthday",
    @SerialName("wifi_ssid") val wifiSsid: String = "",
    @SerialName("wifi_password") val wifiPassword: String = "",
    val volume: Int = 80,
    @SerialName("trigger_direction") val triggerDirection: String = "above",
    @SerialName("trigger_lux") val triggerLux: Double = 300.0,
    @SerialName("dead_zone_lux") val deadZoneLux: Double = 50.0,
    @SerialName("radio_url") val radioUrl: String = "",
    @SerialName("birthday_count") val birthdayCount: Int = 1,
    @SerialName("birthday_file") val birthdayFile: String = "birthday.wav",
    @SerialName("play_on_boot") val playOnBoot: Boolean = false,
    @SerialName("play_boot_loop") val playBootLoop: Boolean = false,
)

@Serializable
data class AudioFile(
    val name: String = "",
    val size: Long = 0,
)

@Serializable
data class Diagnostics(
    @SerialName("bclk_toggled") val bclkToggled: Boolean = false,
    @SerialName("ws_toggled") val wsToggled: Boolean = false,
    @SerialName("dout_toggled") val doutToggled: Boolean = false,
)

@Serializable
data class OkResponse(val ok: Boolean = false)

@Serializable
data class ErrorResponse(
    val ok: Boolean = false,
    val error: String = "",
)
