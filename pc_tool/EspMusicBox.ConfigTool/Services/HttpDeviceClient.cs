using System.Net.Http.Json;
using System.Text.Json;
using EspMusicBox.ConfigTool.Models;

namespace EspMusicBox.ConfigTool.Services;

public sealed class HttpDeviceClient(string baseAddress) : IDeviceClient
{
    private readonly HttpClient _http = new() { BaseAddress = Normalize(baseAddress), Timeout = TimeSpan.FromSeconds(30) };
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    public string Description => _http.BaseAddress!.ToString();

    private static Uri Normalize(string address)
    {
        if (!address.StartsWith("http://", StringComparison.OrdinalIgnoreCase) &&
            !address.StartsWith("https://", StringComparison.OrdinalIgnoreCase)) address = "http://" + address;
        if (!address.EndsWith('/')) address += "/";
        return new Uri(address, UriKind.Absolute);
    }

    public async Task ConnectAsync(CancellationToken cancellationToken = default) =>
        _ = await GetInfoAsync(cancellationToken);

    private async Task<T> GetAsync<T>(string path, CancellationToken token) =>
        await _http.GetFromJsonAsync<T>(path, JsonOptions, token) ?? throw new IOException("设备返回空响应。");

    private static async Task EnsureSuccess(HttpResponseMessage response, CancellationToken token)
    {
        if (response.IsSuccessStatusCode) return;
        string message = await response.Content.ReadAsStringAsync(token);
        throw new HttpRequestException($"设备返回 {(int)response.StatusCode}: {message}");
    }

    public Task<DeviceInfo> GetInfoAsync(CancellationToken cancellationToken = default) =>
        GetAsync<DeviceInfo>("api/info", cancellationToken);

    public Task<DeviceStatus> GetStatusAsync(CancellationToken cancellationToken = default) =>
        GetAsync<DeviceStatus>("api/status", cancellationToken);

    public Task<DeviceConfig> GetConfigAsync(CancellationToken cancellationToken = default) =>
        GetAsync<DeviceConfig>("api/config", cancellationToken);

    public async Task SetConfigAsync(DeviceConfig config, CancellationToken cancellationToken = default)
    {
        using HttpResponseMessage response = await _http.PutAsJsonAsync("api/config", config, JsonOptions, cancellationToken);
        await EnsureSuccess(response, cancellationToken);
    }

    public async Task<IReadOnlyList<AudioFileInfo>> ListFilesAsync(CancellationToken cancellationToken = default) =>
        await GetAsync<List<AudioFileInfo>>("api/files", cancellationToken);

    public async Task UploadAsync(string localPath, IProgress<TransferProgress>? progress = null,
                                  CancellationToken cancellationToken = default)
    {
        await using FileStream stream = File.OpenRead(localPath);
        progress?.Report(new TransferProgress { Sent = 0, Total = stream.Length, BytesPerSecond = 0, Elapsed = TimeSpan.Zero });
        using var content = new StreamContent(stream);
        content.Headers.ContentType = new("application/octet-stream");
        string name = Uri.EscapeDataString(Path.GetFileName(localPath));
        using HttpResponseMessage response = await _http.PostAsync($"api/files?name={name}", content, cancellationToken);
        await EnsureSuccess(response, cancellationToken);
        progress?.Report(new TransferProgress { Sent = stream.Length, Total = stream.Length, BytesPerSecond = 0, Elapsed = TimeSpan.Zero });
    }

    public async Task DeleteAsync(string name, CancellationToken cancellationToken = default)
    {
        using HttpResponseMessage response = await _http.DeleteAsync($"api/files?name={Uri.EscapeDataString(name)}", cancellationToken);
        await EnsureSuccess(response, cancellationToken);
    }

    public async Task PlayAsync(string name, bool loop, CancellationToken cancellationToken = default)
    {
        using HttpResponseMessage response = await _http.PostAsJsonAsync("api/play", new { name, loop }, cancellationToken);
        await EnsureSuccess(response, cancellationToken);
    }

    public async Task StopAsync(CancellationToken cancellationToken = default)
    {
        using HttpResponseMessage response = await _http.PostAsync("api/stop", null, cancellationToken);
        await EnsureSuccess(response, cancellationToken);
    }

    public Task<AudioDiagnostics> GetAudioDiagnosticsAsync(CancellationToken cancellationToken = default) =>
        throw new NotSupportedException("音频诊断仅支持 USB CDC 或 CH340 串口连接。");

    public Task SineTestAsync(int seconds, int bitsPerSample, CancellationToken cancellationToken = default) =>
        throw new NotSupportedException("1kHz 测试音仅支持 USB CDC 或 CH340 串口连接。");

    public Task UpdateFirmwareAsync(string firmwarePath, IProgress<TransferProgress>? progress = null,
                                    CancellationToken cancellationToken = default) =>
        throw new NotSupportedException("固件更新仅支持 USB CDC 或 CH340 串口连接。");

    public ValueTask DisposeAsync()
    {
        _http.Dispose();
        return ValueTask.CompletedTask;
    }
}
