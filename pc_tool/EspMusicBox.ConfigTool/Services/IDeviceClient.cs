using EspMusicBox.ConfigTool.Models;

namespace EspMusicBox.ConfigTool.Services;

public interface IDeviceClient : IAsyncDisposable
{
    string Description { get; }
    Task ConnectAsync(CancellationToken cancellationToken = default);
    Task<DeviceInfo> GetInfoAsync(CancellationToken cancellationToken = default);
    Task<DeviceStatus> GetStatusAsync(CancellationToken cancellationToken = default);
    Task<DeviceConfig> GetConfigAsync(CancellationToken cancellationToken = default);
    Task SetConfigAsync(DeviceConfig config, CancellationToken cancellationToken = default);
    Task<IReadOnlyList<AudioFileInfo>> ListFilesAsync(CancellationToken cancellationToken = default);
    Task UploadAsync(string localPath, IProgress<TransferProgress>? progress = null, CancellationToken cancellationToken = default);
    Task DeleteAsync(string name, CancellationToken cancellationToken = default);
    Task PlayAsync(string name, bool loop, CancellationToken cancellationToken = default);
    Task StopAsync(CancellationToken cancellationToken = default);
    Task<AudioDiagnostics> GetAudioDiagnosticsAsync(CancellationToken cancellationToken = default);
    Task SineTestAsync(int seconds, int bitsPerSample, CancellationToken cancellationToken = default);
    Task UpdateFirmwareAsync(string firmwarePath, IProgress<TransferProgress>? progress = null,
                             CancellationToken cancellationToken = default);
}
