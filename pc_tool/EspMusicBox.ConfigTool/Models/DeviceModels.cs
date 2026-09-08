using System.Text.Json.Serialization;

namespace EspMusicBox.ConfigTool.Models;

public sealed class DeviceInfo
{
    [JsonPropertyName("device_name")] public string DeviceName { get; set; } = "";
    [JsonPropertyName("mac")] public string Mac { get; set; } = "";
    [JsonPropertyName("firmware_version")] public string FirmwareVersion { get; set; } = "";
    [JsonPropertyName("flash_size")] public long FlashSize { get; set; }
}

public sealed class DeviceConfig
{
    [JsonPropertyName("mode")] public string Mode { get; set; } = "birthday";
    [JsonPropertyName("wifi_ssid")] public string WifiSsid { get; set; } = "";
    [JsonPropertyName("wifi_password")] public string WifiPassword { get; set; } = "";
    [JsonPropertyName("volume")] public int Volume { get; set; } = 80;
    [JsonPropertyName("lux_threshold")] public double LuxThreshold { get; set; } = 200;
    [JsonPropertyName("radio_url")] public string RadioUrl { get; set; } = "";
    [JsonPropertyName("birthday_count")] public int BirthdayCount { get; set; } = 1;
    [JsonPropertyName("birthday_file")] public string BirthdayFile { get; set; } = "birthday.wav";
    [JsonPropertyName("play_on_boot")] public bool PlayOnBoot { get; set; }
    [JsonPropertyName("play_boot_loop")] public bool PlayBootLoop { get; set; }
}

public sealed class DeviceStatus
{
    [JsonPropertyName("lux")] public double Lux { get; set; }
    [JsonPropertyName("mode")] public string Mode { get; set; } = "";
    [JsonPropertyName("playback")] public string Playback { get; set; } = "";
    [JsonPropertyName("playback_source")] public string PlaybackSource { get; set; } = "";
    [JsonPropertyName("playback_loop")] public bool PlaybackLoop { get; set; }
    [JsonPropertyName("wifi_connected")] public bool WifiConnected { get; set; }
    [JsonPropertyName("ip_address")] public string IpAddress { get; set; } = "";
    [JsonPropertyName("littlefs_total")] public long LittleFsTotal { get; set; }
    [JsonPropertyName("littlefs_free")] public long LittleFsFree { get; set; }
    [JsonPropertyName("last_error")] public string LastError { get; set; } = "";
}

public sealed class AudioFileInfo
{
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("size")] public long Size { get; set; }
}

public sealed class AudioDiagnostics
{
    [JsonPropertyName("bclk_toggled")] public bool BclkToggled { get; set; }
    [JsonPropertyName("ws_toggled")] public bool WsToggled { get; set; }
    [JsonPropertyName("dout_toggled")] public bool DoutToggled { get; set; }
}

public sealed class TransferProgress
{
    public long Sent { get; init; }
    public long Total { get; init; }
    public double BytesPerSecond { get; init; }
    public TimeSpan Elapsed { get; init; }
    public int Percent => Total <= 0 ? 100 : (int)Math.Clamp(Sent * 100 / Total, 0, 100);
}
