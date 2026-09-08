using System.Diagnostics;
using System.IO.Ports;
using System.Text.Json;
using System.Text.Json.Nodes;
using EspMusicBox.ConfigTool.Models;

namespace EspMusicBox.ConfigTool.Services;

public sealed class SerialDeviceClient(string portName, string connectionMode = "USB CDC") : IDeviceClient
{
    private const int PipelineWindow = 16;
    private const int ChunkSize = 2880;
    private readonly SerialPort _port = new(portName, 115200)
    {
        NewLine = "\n",
        ReadTimeout = 7000,
        WriteTimeout = 30000,
        DtrEnable = false,
        RtsEnable = false
    };
    private readonly SemaphoreSlim _requestLock = new(1, 1);
    private int _nextId;
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    public string Description => $"{connectionMode} ({portName})";
    public string PortName => portName;
    public string ConnectionMode => connectionMode;

    public async Task ConnectAsync(CancellationToken cancellationToken = default)
    {
        _port.Open();
        _port.DiscardInBuffer();
        JsonNode? pong = await RequestAsync("ping", null, cancellationToken);
        if (pong?.GetValue<string>() != "pong") throw new IOException("设备没有返回有效的 pong。请确认选中了 ESP Music Box 的 CDC 端口。");
    }

    private async Task<JsonNode?> RequestAsync(string command, JsonNode? data,
                                                CancellationToken cancellationToken)
    {
        await _requestLock.WaitAsync(cancellationToken);
        try
        {
            int id = Interlocked.Increment(ref _nextId);
            var request = new JsonObject { ["id"] = id, ["cmd"] = command };
            if (data is not null) request["data"] = data;
            string responseLine = await SendAndReadAsync(request, id, cancellationToken);
            JsonObject response = JsonNode.Parse(responseLine)?.AsObject()
                                  ?? throw new IOException("设备返回了无效 JSON。");
            if (response["id"]?.GetValue<int>() != id) throw new IOException("设备响应序号不匹配。");
            if (response["ok"]?.GetValue<bool>() != true)
                throw new IOException(response["error"]?.GetValue<string>() ?? "设备拒绝了请求。");
            return response["data"];
        }
        finally
        {
            _requestLock.Release();
        }
    }

    private async Task SendPipelinedAsync(List<JsonObject> requests, CancellationToken cancellationToken)
    {
        if (requests.Count == 0) return;
        await _requestLock.WaitAsync(cancellationToken);
        try
        {
            int[] ids = new int[requests.Count];
            for (int i = 0; i < requests.Count; ++i)
            {
                ids[i] = Interlocked.Increment(ref _nextId);
                requests[i]["id"] = ids[i];
            }
            await Task.Run(() =>
            {
                foreach (JsonObject request in requests)
                    _port.WriteLine(request.ToJsonString(JsonOptions));
            }, cancellationToken);
            foreach (int id in ids)
            {
                string responseLine = await ReadResponseLineAsync(id, cancellationToken);
                JsonObject response = JsonNode.Parse(responseLine)?.AsObject()
                                      ?? throw new IOException("设备返回了无效 JSON。");
                if (response["ok"]?.GetValue<bool>() != true)
                    throw new IOException(response["error"]?.GetValue<string>() ?? "设备拒绝了请求。");
            }
        }
        finally
        {
            _requestLock.Release();
        }
    }

    private async Task<string> SendAndReadAsync(JsonObject request, int id, CancellationToken cancellationToken)
    {
        string line = request.ToJsonString(JsonOptions);
        await Task.Run(() => _port.WriteLine(line), cancellationToken);
        return await ReadResponseLineAsync(id, cancellationToken);
    }

    private async Task<string> ReadResponseLineAsync(int id, CancellationToken cancellationToken)
    {
        return await Task.Run(() =>
        {
            for (;;)
            {
                string candidate = _port.ReadLine().Trim();
                if (!candidate.StartsWith('{')) continue; // CH340 上会同时看到启动日志
                try
                {
                    JsonObject? parsed = JsonNode.Parse(candidate)?.AsObject();
                    if (parsed?["id"]?.GetValue<int>() == id) return candidate;
                }
                catch (JsonException) { }
            }
        }, cancellationToken).WaitAsync(cancellationToken);
    }

    private async Task<T> RequestValueAsync<T>(string command, JsonNode? data,
                                                CancellationToken cancellationToken)
        where T : class
    {
        JsonNode? node = await RequestAsync(command, data, cancellationToken);
        if (node is null) throw new IOException("设备响应缺少 data。");
        return node.Deserialize<T>(JsonOptions) ?? throw new IOException("设备响应 data 无效。");
    }

    public Task<DeviceInfo> GetInfoAsync(CancellationToken cancellationToken = default) =>
        RequestValueAsync<DeviceInfo>("info.get", null, cancellationToken);

    public Task<DeviceStatus> GetStatusAsync(CancellationToken cancellationToken = default) =>
        RequestValueAsync<DeviceStatus>("status.get", null, cancellationToken);

    public Task<DeviceConfig> GetConfigAsync(CancellationToken cancellationToken = default) =>
        RequestValueAsync<DeviceConfig>("config.get", null, cancellationToken);

    public async Task SetConfigAsync(DeviceConfig config, CancellationToken cancellationToken = default) =>
        _ = await RequestAsync("config.set", JsonSerializer.SerializeToNode(config, JsonOptions), cancellationToken);

    public async Task<IReadOnlyList<AudioFileInfo>> ListFilesAsync(CancellationToken cancellationToken = default) =>
        await RequestValueAsync<List<AudioFileInfo>>("file.list", null, cancellationToken);

    public async Task UploadAsync(string localPath, IProgress<TransferProgress>? progress = null,
                                  CancellationToken cancellationToken = default)
    {
        var file = new FileInfo(localPath);
        try { await RequestAsync("file.abort", null, cancellationToken); } catch { }
        await RequestAsync("file.begin", new JsonObject { ["name"] = file.Name, ["size"] = file.Length }, cancellationToken);
        var stopwatch = Stopwatch.StartNew();
        try
        {
            await using FileStream stream = file.OpenRead();
            byte[] buffer = new byte[ChunkSize];
            long sent = 0;
            var window = new List<JsonObject>();
            while (true)
            {
                int count = await stream.ReadAsync(buffer, cancellationToken);
                if (count == 0) break;
                window.Add(new JsonObject
                {
                    ["cmd"] = "file.chunk",
                    ["data"] = new JsonObject { ["base64"] = Convert.ToBase64String(buffer, 0, count) }
                });
                sent += count;
                if (window.Count >= PipelineWindow)
                {
                    await SendPipelinedAsync(window, cancellationToken);
                    window.Clear();
                    progress?.Report(MakeProgress(sent, file.Length, stopwatch));
                }
            }
            if (window.Count > 0) await SendPipelinedAsync(window, cancellationToken);
            await RequestAsync("file.end", null, cancellationToken);
            progress?.Report(MakeProgress(file.Length, file.Length, stopwatch));
        }
        catch
        {
            try { await RequestAsync("file.abort", null, CancellationToken.None); } catch { }
            throw;
        }
    }

    public async Task DeleteAsync(string name, CancellationToken cancellationToken = default) =>
        _ = await RequestAsync("file.delete", new JsonObject { ["name"] = name }, cancellationToken);

    public async Task PlayAsync(string name, bool loop, CancellationToken cancellationToken = default) =>
        _ = await RequestAsync("audio.play", new JsonObject { ["name"] = name, ["loop"] = loop }, cancellationToken);

    public async Task StopAsync(CancellationToken cancellationToken = default) =>
        _ = await RequestAsync("audio.stop", null, cancellationToken);

    public Task<AudioDiagnostics> GetAudioDiagnosticsAsync(CancellationToken cancellationToken = default) =>
        RequestValueAsync<AudioDiagnostics>("audio.diagnostics", null, cancellationToken);

    public async Task SineTestAsync(int seconds, int bitsPerSample, CancellationToken cancellationToken = default) =>
        _ = await RequestAsync("audio.sine", new JsonObject { ["seconds"] = seconds, ["bits"] = bitsPerSample }, cancellationToken);

    public async Task UpdateFirmwareAsync(string firmwarePath, IProgress<TransferProgress>? progress = null,
                                          CancellationToken cancellationToken = default)
    {
        var file = new FileInfo(firmwarePath);
        if (!file.Exists || file.Length == 0) throw new FileNotFoundException("固件文件不存在。", firmwarePath);
        try { await RequestAsync("ota.abort", null, cancellationToken); } catch { }
        await RequestAsync("ota.begin", new JsonObject { ["size"] = file.Length }, cancellationToken);
        var stopwatch = Stopwatch.StartNew();
        try
        {
            await using FileStream stream = file.OpenRead();
            byte[] buffer = new byte[ChunkSize];
            long sent = 0;
            var window = new List<JsonObject>();
            while (true)
            {
                int count = await stream.ReadAsync(buffer, cancellationToken);
                if (count == 0) break;
                window.Add(new JsonObject
                {
                    ["cmd"] = "ota.chunk",
                    ["data"] = new JsonObject { ["base64"] = Convert.ToBase64String(buffer, 0, count) }
                });
                sent += count;
                if (window.Count >= PipelineWindow)
                {
                    await SendPipelinedAsync(window, cancellationToken);
                    window.Clear();
                    progress?.Report(MakeProgress(sent, file.Length, stopwatch));
                }
            }
            if (window.Count > 0) await SendPipelinedAsync(window, cancellationToken);
            await RequestAsync("ota.end", null, cancellationToken);
            progress?.Report(MakeProgress(file.Length, file.Length, stopwatch));
        }
        catch
        {
            try { await RequestAsync("ota.abort", null, CancellationToken.None); } catch { }
            throw;
        }
    }

    private static TransferProgress MakeProgress(long sent, long total, Stopwatch stopwatch) => new()
    {
        Sent = sent,
        Total = total,
        BytesPerSecond = sent / stopwatch.Elapsed.TotalSeconds,
        Elapsed = stopwatch.Elapsed
    };

    public ValueTask DisposeAsync()
    {
        if (_port.IsOpen) _port.Close();
        _port.Dispose();
        _requestLock.Dispose();
        return ValueTask.CompletedTask;
    }
}
