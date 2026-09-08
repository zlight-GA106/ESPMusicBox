using System.IO.Ports;
using EspMusicBox.ConfigTool.Models;
using EspMusicBox.ConfigTool.Services;

namespace EspMusicBox.ConfigTool;

public sealed class MainForm : Form
{
    private readonly ComboBox _ports = new() { Width = 100, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly TextBox _httpAddress = new() { Width = 150, Text = "http://192.168.1.100/" };
    private readonly Label _connection = new() { AutoSize = true, Text = "未连接", ForeColor = Color.Firebrick, Padding = new(8, 7, 0, 0) };
    private readonly Label _deviceInfo = new() { Dock = DockStyle.Top, Height = 28, Text = "设备：-", Padding = new(8, 5, 0, 0) };
    private readonly TextBox _ssid = new() { Dock = DockStyle.Fill };
    private readonly TextBox _password = new() { Dock = DockStyle.Fill, UseSystemPasswordChar = true };
    private readonly NumericUpDown _volume = new() { Minimum = 0, Maximum = 100, Value = 80, Width = 100 };
    private readonly RadioButton _birthdayMode = new() { Text = "生日模式", AutoSize = true, Checked = true };
    private readonly RadioButton _radioMode = new() { Text = "网络电台", AutoSize = true };
    private readonly NumericUpDown _luxThreshold = new() { Minimum = 1, Maximum = 100000, DecimalPlaces = 1, Value = 200, Width = 120 };
    private readonly NumericUpDown _birthdayCount = new() { Minimum = 1, Maximum = 100, Value = 1, Width = 100 };
    private readonly CheckBox _playOnBoot = new() { Text = "通电后直接播放（忽略 BH1750）", AutoSize = true };
    private readonly CheckBox _playBootLoop = new() { Text = "上电播放无限循环", AutoSize = true };
    private readonly TextBox _radioUrl = new() { Dock = DockStyle.Fill };
    private readonly ListView _files = new() { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, MultiSelect = false, CheckBoxes = true };
    private readonly CheckBox _loopPlayback = new() { Text = "单曲无限循环", AutoSize = true, Padding = new Padding(4, 6, 0, 0) };
    private readonly ProgressBar _uploadProgress = new() { Width = 140, Visible = false };
    private readonly Label _uploadInfo = new() { AutoSize = true, Visible = false, Padding = new Padding(8, 7, 0, 0) };
    private readonly Label _lux = StatusLabel();
    private readonly Label _mode = StatusLabel();
    private readonly Label _playback = StatusLabel();
    private readonly Label _wifi = StatusLabel();
    private readonly Label _storage = StatusLabel();
    private readonly Label _error = StatusLabel();
    private readonly Label _audioDiag = StatusLabel();
    private readonly TextBox _log = new() { Dock = DockStyle.Bottom, Height = 110, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical };
    private readonly System.Windows.Forms.Timer _statusTimer = new() { Interval = 2000 };
    private readonly RadioButton _updateCdc = new() { Text = "USB CDC", AutoSize = true, Checked = true };
    private readonly RadioButton _updateCh340 = new() { Text = "CH340", AutoSize = true };
    private readonly RadioButton _uploadSerial = new() { Text = "串口", AutoSize = true, Checked = true };
    private readonly RadioButton _uploadCdc = new() { Text = "CDC", AutoSize = true };
    private readonly TextBox _firmwarePath = new() { Dock = DockStyle.Fill, ReadOnly = true };
    private readonly ProgressBar _firmwareProgress = new() { Width = 260, Visible = false };
    private readonly Label _firmwareInfo = new() { AutoSize = true, Visible = false, Padding = new Padding(8, 7, 0, 0) };
    private readonly NumericUpDown _sineSeconds = new() { Minimum = 1, Maximum = 30, Value = 5, Width = 60 };
    private IDeviceClient? _client;
    private bool _refreshing;
    private bool _syncingFileChecks;
    private string _birthdayFileName = "birthday.wav";

    public MainForm()
    {
        Text = "ESP32-S3 智能生日音箱配置工具";
        StartPosition = FormStartPosition.CenterScreen;
        MinimumSize = new Size(860, 600);
        Size = new Size(1040, 700);
        Font = new Font("Microsoft YaHei UI", 9F);

        Controls.Add(BuildTabs());
        Controls.Add(_deviceInfo);
        Controls.Add(BuildConnectionBar());
        Controls.Add(_log);
        RefreshPorts();
        _statusTimer.Tick += async (_, _) => await RefreshStatusAsync();
        FormClosed += (_, _) => _client?.DisposeAsync().AsTask().GetAwaiter().GetResult();
    }

    private Control BuildConnectionBar()
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 42, Padding = new Padding(7, 5, 5, 3), WrapContents = false };
        var refresh = new Button { Text = "刷新端口", AutoSize = true };
        var cdc = new Button { Text = "CDC连接", AutoSize = true };
        var ch340 = new Button { Text = "CH340连接", AutoSize = true };
        var http = new Button { Text = "HTTP连接", AutoSize = true };
        var disconnect = new Button { Text = "断开连接", AutoSize = true };
        refresh.Click += (_, _) => RefreshPorts();
        cdc.Click += async (_, _) => await ConnectSerialAsync("USB CDC");
        ch340.Click += async (_, _) => await ConnectSerialAsync("CH340");
        http.Click += async (_, _) => await ConnectHttpAsync();
        disconnect.Click += async (_, _) => await DisconnectAsync();
        panel.Controls.AddRange([new Label { Text = "串口", AutoSize = true, Padding = new Padding(0, 7, 0, 0) },
                                 _ports, refresh, cdc, ch340,
                                 new Label { Text = "  设备地址", AutoSize = true, Padding = new Padding(0, 7, 0, 0) },
                                 _httpAddress, http, disconnect, _connection]);
        return panel;
    }

    private Control BuildTabs()
    {
        var tabs = new TabControl { Dock = DockStyle.Fill };
        tabs.TabPages.Add(new TabPage("配置") { Controls = { BuildConfigPage() } });
        tabs.TabPages.Add(new TabPage("音频管理") { Controls = { BuildAudioPage() } });
        tabs.TabPages.Add(new TabPage("设备状态") { Controls = { BuildStatusPage() } });
        tabs.TabPages.Add(new TabPage("固件更新") { Controls = { BuildFirmwarePage() } });
        return tabs;
    }

    private Control BuildConfigPage()
    {
        var table = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(16), ColumnCount = 2, RowCount = 10 };
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 125));
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        AddConfigRow(table, 0, "WiFi SSID", _ssid);
        AddConfigRow(table, 1, "WiFi Password", _password);
        AddConfigRow(table, 2, "音量 (0-100)", _volume);
        var modes = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        modes.Controls.AddRange([_birthdayMode, _radioMode]);
        AddConfigRow(table, 3, "工作模式", modes);
        AddConfigRow(table, 4, "触发变化 (Lux)", _luxThreshold);
        AddConfigRow(table, 5, "生日歌播放次数", _birthdayCount);
        var boot = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        boot.Controls.AddRange([_playOnBoot, _playBootLoop]);
        AddConfigRow(table, 6, "上电播放", boot);
        AddConfigRow(table, 7, "电台 URL", _radioUrl);
        var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        var load = new Button { Text = "从设备读取", AutoSize = true };
        var save = new Button { Text = "保存到设备", AutoSize = true };
        load.Click += async (_, _) => await RunAsync(LoadConfigAsync, "读取配置");
        save.Click += async (_, _) => await RunAsync(SaveConfigAsync, "保存配置");
        buttons.Controls.AddRange([load, save]);
        AddConfigRow(table, 8, "", buttons);
        var note = new Label { Text = "生日触发条件：当前 Lux 与稳定基线的绝对差值达到阈值；回到阈值的 40% 内后重新武装。", AutoSize = true, ForeColor = Color.DimGray };
        AddConfigRow(table, 9, "说明", note);
        return table;
    }

    private static void AddConfigRow(TableLayoutPanel table, int row, string caption, Control control)
    {
        table.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        table.Controls.Add(new Label { Text = caption, AutoSize = true, Padding = new Padding(0, 7, 0, 8) }, 0, row);
        control.Margin = new Padding(3, 3, 3, 8);
        table.Controls.Add(control, 1, row);
    }

    private Control BuildAudioPage()
    {
        _files.Columns.Add("文件名（勾选即设为生日歌）", 450);
        _files.Columns.Add("大小", 130);
        _files.Columns.Add("用途", 100);
        _files.ItemChecked += AudioFileItemChecked;
        var panel = new Panel { Dock = DockStyle.Fill, Padding = new Padding(8) };
        var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, Height = 44 };
        var refresh = new Button { Text = "刷新列表", AutoSize = true };
        var upload = new Button { Text = "选择并上传", AutoSize = true };
        var delete = new Button { Text = "删除", AutoSize = true };
        var play = new Button { Text = "播放", AutoSize = true };
        var stop = new Button { Text = "停止", AutoSize = true };
        var sine = new Button { Text = "1kHz 测试音", AutoSize = true };
        refresh.Click += async (_, _) => await RunAsync(RefreshFilesAsync, "刷新文件");
        upload.Click += async (_, _) => await UploadAsync();
        delete.Click += async (_, _) => await WithSelectedFileAsync((client, name) => client.DeleteAsync(name), "删除");
        play.Click += async (_, _) => await PlaySelectedFileAsync();
        stop.Click += async (_, _) => await RunAsync(() => RequireClient().StopAsync(), "停止播放");
        sine.Click += async (_, _) => await RunSineTestAsync();
        buttons.Controls.AddRange([refresh, upload,
                                   new Label { Text = " 上传通道", AutoSize = true, Padding = new Padding(6, 7, 0, 0) },
                                   _uploadSerial, _uploadCdc,
                                   delete, play, stop, sine,
                                   new Label { Text = " 时长(s)", AutoSize = true, Padding = new Padding(6, 7, 0, 0) },
                                   _sineSeconds, _loopPlayback, _uploadProgress, _uploadInfo]);
        panel.Controls.Add(_files);
        panel.Controls.Add(buttons);
        return panel;
    }

    private Control BuildStatusPage()
    {
        var table = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(25), ColumnCount = 2, RowCount = 8 };
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 140));
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        string[] names = ["当前光照", "当前模式", "播放状态", "WiFi 状态", "LittleFS 空间", "最近错误"];
        Label[] values = [_lux, _mode, _playback, _wifi, _storage, _error];
        for (int i = 0; i < names.Length; ++i) AddConfigRow(table, i, names[i], values[i]);
        var diag = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        var diagButton = new Button { Text = "刷新诊断", AutoSize = true };
        diagButton.Click += async (_, _) => await RunAsync(RefreshDiagnosticsAsync, "刷新诊断");
        diag.Controls.AddRange([_audioDiag, diagButton,
                                new Label { Text = "  （需先播放音频，计数器才会更新）", AutoSize = true, ForeColor = Color.DimGray }]);
        AddConfigRow(table, 6, "I2S 信号", diag);
        var refresh = new Button { Text = "立即刷新", AutoSize = true };
        refresh.Click += async (_, _) => await RunAsync(() => RefreshStatusAsync(true), "刷新状态");
        AddConfigRow(table, 7, "", refresh);
        return table;
    }

    private Control BuildFirmwarePage()
    {
        var table = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(24), ColumnCount = 2, RowCount = 4 };
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 130));
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));

        var modes = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        modes.Controls.AddRange([_updateCdc, _updateCh340]);
        AddConfigRow(table, 0, "更新连接方式", modes);

        var filePanel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, AutoSize = true };
        filePanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        filePanel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        var browse = new Button { Text = "选择 .bin", AutoSize = true };
        browse.Click += (_, _) => BrowseFirmware();
        filePanel.Controls.Add(_firmwarePath, 0, 0);
        filePanel.Controls.Add(browse, 1, 0);
        AddConfigRow(table, 1, "应用固件", filePanel);

        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        var update = new Button { Text = "开始更新", AutoSize = true };
        update.Click += async (_, _) => await UpdateFirmwareAsync();
        actions.Controls.AddRange([update, _firmwareProgress, _firmwareInfo]);
        AddConfigRow(table, 2, "", actions);

        var note = new Label
        {
            AutoSize = true,
            MaximumSize = new Size(680, 0),
            ForeColor = Color.DimGray,
            Text = "选择顶部串口及 CDC/CH340 连接方式。更新写入备用 OTA 分区，完成后设备自动重启；请勿在更新过程中断电。\r\n\r\nmade by zlight106"
        };
        AddConfigRow(table, 3, "说明", note);
        return table;
    }

    private static Label StatusLabel() => new() { AutoSize = true, Text = "-", Padding = new Padding(0, 7, 0, 0) };

    private void RefreshPorts()
    {
        string? selected = _ports.SelectedItem?.ToString();
        _ports.Items.Clear();
        _ports.Items.AddRange(SerialPort.GetPortNames().OrderBy(x => x).ToArray());
        if (selected is not null && _ports.Items.Contains(selected)) _ports.SelectedItem = selected;
        else if (_ports.Items.Count > 0) _ports.SelectedIndex = 0;
    }

    private async Task ConnectSerialAsync(string connectionMode)
    {
        if (_ports.SelectedItem is not string port) { MessageBox.Show("没有可用串口。", "提示"); return; }
        await ConnectAsync(new SerialDeviceClient(port, connectionMode));
    }

    private async Task ConnectHttpAsync() => await ConnectAsync(new HttpDeviceClient(_httpAddress.Text.Trim()));

    private async Task ConnectAsync(IDeviceClient candidate)
    {
        try
        {
            await candidate.ConnectAsync();
            if (_client is not null) await _client.DisposeAsync();
            _client = candidate;
            _connection.Text = $"已连接：{candidate.Description}";
            _connection.ForeColor = Color.DarkGreen;
            DeviceInfo info = await candidate.GetInfoAsync();
            _deviceInfo.Text = $"设备：{info.DeviceName}    MAC：{info.Mac}    固件：{info.FirmwareVersion}    Flash：{FormatBytes(info.FlashSize)}";
            await LoadConfigAsync();
            await RefreshFilesAsync();
            await RefreshStatusAsync(true);
            _statusTimer.Start();
            Log("连接成功。", false);
        }
        catch (Exception ex)
        {
            await candidate.DisposeAsync();
            Log($"连接失败：{ex.Message}", true);
            MessageBox.Show(ex.Message, "连接失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private IDeviceClient RequireClient() => _client ?? throw new InvalidOperationException("请先连接设备。");

    private async Task DisconnectAsync()
    {
        if (_client is null) return;
        _statusTimer.Stop();
        IDeviceClient client = _client;
        _client = null;
        await client.DisposeAsync();
        _connection.Text = "未连接";
        _connection.ForeColor = Color.Firebrick;
        _deviceInfo.Text = "设备：-";
        Log("已断开连接。", false);
    }

    private async Task LoadConfigAsync()
    {
        DeviceConfig config = await RequireClient().GetConfigAsync();
        _ssid.Text = config.WifiSsid;
        _password.Text = config.WifiPassword;
        _volume.Value = Math.Clamp(config.Volume, 0, 100);
        _birthdayMode.Checked = config.Mode == "birthday";
        _radioMode.Checked = config.Mode == "radio";
        _luxThreshold.Value = (decimal)Math.Clamp(config.LuxThreshold, 1, 100000);
        _birthdayCount.Value = Math.Clamp(config.BirthdayCount, 1, 100);
        _playOnBoot.Checked = config.PlayOnBoot;
        _playBootLoop.Checked = config.PlayBootLoop;
        _birthdayFileName = string.IsNullOrWhiteSpace(config.BirthdayFile) ? "birthday.wav" : config.BirthdayFile;
        _radioUrl.Text = config.RadioUrl;
    }

    private async Task SaveConfigAsync()
    {
        var config = new DeviceConfig
        {
            Mode = _radioMode.Checked ? "radio" : "birthday",
            WifiSsid = _ssid.Text,
            WifiPassword = _password.Text,
            Volume = (int)_volume.Value,
            LuxThreshold = (double)_luxThreshold.Value,
            BirthdayCount = (int)_birthdayCount.Value,
            BirthdayFile = _birthdayFileName,
            PlayOnBoot = _playOnBoot.Checked,
            PlayBootLoop = _playBootLoop.Checked,
            RadioUrl = _radioUrl.Text.Trim()
        };
        await RequireClient().SetConfigAsync(config);
    }

    private async Task RefreshFilesAsync()
    {
        IReadOnlyList<AudioFileInfo> files = await RequireClient().ListFilesAsync();
        _syncingFileChecks = true;
        _files.BeginUpdate();
        _files.Items.Clear();
        foreach (AudioFileInfo file in files)
        {
            bool isBirthday = string.Equals(file.Name, _birthdayFileName, StringComparison.OrdinalIgnoreCase);
            _files.Items.Add(new ListViewItem([file.Name, FormatBytes(file.Size), isBirthday ? "生日歌" : ""])
            {
                Checked = isBirthday
            });
        }
        _files.EndUpdate();
        _syncingFileChecks = false;
    }

    private async Task UploadAsync()
    {
        using var dialog = new OpenFileDialog { Filter = "音频文件 (*.wav;*.mp3)|*.wav;*.mp3", CheckFileExists = true };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        if (_ports.SelectedItem is not string port) { MessageBox.Show("请先在顶部选择串口。", "提示"); return; }
        string mode = _uploadCdc.Checked ? "USB CDC" : "CH340";
        _uploadProgress.Value = 0;
        _uploadProgress.Visible = true;
        _uploadInfo.Visible = true;
        _uploadInfo.Text = $"准备上传（{mode}）…";
        var lastPaint = DateTime.MinValue;
        IDeviceClient? transient = null;
        try
        {
            IDeviceClient client;
            if (_client is SerialDeviceClient serial && serial.PortName == port && serial.ConnectionMode == mode)
            {
                client = serial;
            }
            else
            {
                if (_client is SerialDeviceClient held && held.PortName == port)
                {
                    throw new InvalidOperationException($"串口 {port} 已作为 {held.ConnectionMode} 通道连接，请选择与其一致的传输通道，或更换串口。");
                }
                transient = new SerialDeviceClient(port, mode);
                await transient.ConnectAsync();
                client = transient;
            }

            var progress = new Progress<TransferProgress>(p =>
            {
                _uploadProgress.Value = p.Percent;
                if ((DateTime.Now - lastPaint).TotalMilliseconds >= 200 || p.Sent >= p.Total)
                {
                    lastPaint = DateTime.Now;
                    _uploadInfo.Text = FormatTransfer(p);
                }
            });
            await client.UploadAsync(dialog.FileName, progress);
            await RefreshFilesAsync();
            Log($"已上传 {Path.GetFileName(dialog.FileName)}。", false);
        }
        catch (Exception ex)
        {
            Log($"上传失败：{ex.Message}", true);
            MessageBox.Show(ex.Message, "上传失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            if (transient is not null) await transient.DisposeAsync();
            _uploadProgress.Visible = false;
            _uploadInfo.Visible = false;
        }
    }

    private async void AudioFileItemChecked(object? sender, ItemCheckedEventArgs e)
    {
        if (_syncingFileChecks) return;
        if (!e.Item.Checked)
        {
            if (string.Equals(e.Item.Text, _birthdayFileName, StringComparison.OrdinalIgnoreCase))
            {
                _syncingFileChecks = true;
                e.Item.Checked = true;
                _syncingFileChecks = false;
            }
            return;
        }

        _syncingFileChecks = true;
        foreach (ListViewItem item in _files.Items)
            if (!ReferenceEquals(item, e.Item)) item.Checked = false;
        _syncingFileChecks = false;

        try
        {
            DeviceConfig config = await RequireClient().GetConfigAsync();
            config.BirthdayFile = e.Item.Text;
            await RequireClient().SetConfigAsync(config);
            _birthdayFileName = e.Item.Text;
            foreach (ListViewItem item in _files.Items)
                item.SubItems[2].Text = ReferenceEquals(item, e.Item) ? "生日歌" : "";
            Log($"已将 {e.Item.Text} 设置为生日歌。", false);
        }
        catch (Exception ex)
        {
            Log($"设置生日歌失败：{ex.Message}", true);
            try { await RefreshFilesAsync(); } catch { }
            MessageBox.Show(ex.Message, "设置生日歌失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private async Task PlaySelectedFileAsync()
    {
        if (_files.SelectedItems.Count == 0) { MessageBox.Show("请先选择一个文件。", "提示"); return; }
        string name = _files.SelectedItems[0].Text;
        bool loop = _loopPlayback.Checked;
        await RunAsync(async () =>
        {
            IDeviceClient client = RequireClient();
            await client.PlayAsync(name, loop);
            await Task.Delay(350);
            DeviceStatus status = await client.GetStatusAsync();
            await RefreshStatusAsync(true);
            if (status.Playback == "error")
                throw new InvalidOperationException(string.IsNullOrEmpty(status.LastError) ? "设备无法解码该音频。" : status.LastError);
        }, loop ? "单曲循环命令发送" : "播放命令发送");
    }

    private void BrowseFirmware()
    {
        using var dialog = new OpenFileDialog { Filter = "ESP-IDF 应用固件 (*.bin)|*.bin", CheckFileExists = true };
        if (dialog.ShowDialog(this) == DialogResult.OK) _firmwarePath.Text = dialog.FileName;
    }

    private async Task RefreshDiagnosticsAsync()
    {
        AudioDiagnostics diag = await RequireClient().GetAudioDiagnosticsAsync();
        static string On(bool value) => value ? "翻转" : "静止";
        _audioDiag.Text = $"BCLK: {On(diag.BclkToggled)}   WS: {On(diag.WsToggled)}   DIN: {On(diag.DoutToggled)}";
    }

    private async Task RunSineTestAsync()
    {
        try
        {
            _statusTimer.Stop();
            await RequireClient().SineTestAsync((int)_sineSeconds.Value, 16);
            Log("测试音命令已发送，设备即将重启并播放 1kHz 测试音。", false);
            await RequireClient().DisposeAsync();
            _client = null;
            _connection.Text = "设备重启中，请重新连接";
            _connection.ForeColor = Color.DarkGreen;
        }
        catch (Exception ex)
        {
            Log($"发送测试音失败：{ex.Message}", true);
            MessageBox.Show(ex.Message, "发送测试音失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private async Task UpdateFirmwareAsync()
    {
        if (!File.Exists(_firmwarePath.Text)) { MessageBox.Show("请先选择应用固件 .bin 文件。", "提示"); return; }
        if (_ports.SelectedItem is not string port) { MessageBox.Show("请选择串口。", "提示"); return; }
        string mode = _updateCh340.Checked ? "CH340" : "USB CDC";
        try
        {
            if (_client is not SerialDeviceClient serial || serial.PortName != port || serial.ConnectionMode != mode)
            {
                if (_client is not null) await _client.DisposeAsync();
                var candidate = new SerialDeviceClient(port, mode);
                await candidate.ConnectAsync();
                _client = candidate;
                _connection.Text = $"已连接：{candidate.Description}";
                _connection.ForeColor = Color.DarkGreen;
            }

            _statusTimer.Stop();
            _firmwareProgress.Value = 0;
            _firmwareProgress.Visible = true;
            _firmwareInfo.Visible = true;
            _firmwareInfo.Text = "准备更新…";
            var lastPaint = DateTime.MinValue;
            var progress = new Progress<TransferProgress>(p =>
            {
                _firmwareProgress.Value = p.Percent;
                if ((DateTime.Now - lastPaint).TotalMilliseconds >= 200 || p.Sent >= p.Total)
                {
                    lastPaint = DateTime.Now;
                    _firmwareInfo.Text = FormatTransfer(p);
                }
            });
            await RequireClient().UpdateFirmwareAsync(_firmwarePath.Text, progress);
            Log("固件写入成功，设备正在重启。", false);
            await RequireClient().DisposeAsync();
            _client = null;
            _connection.Text = "固件更新完成，请重新连接";
            _connection.ForeColor = Color.DarkGreen;
        }
        catch (Exception ex)
        {
            Log($"固件更新失败：{ex.Message}", true);
            MessageBox.Show(ex.Message, "固件更新失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            _firmwareProgress.Visible = false;
            _firmwareInfo.Visible = false;
        }
    }

    private async Task WithSelectedFileAsync(Func<IDeviceClient, string, Task> action, string operation)
    {
        if (_files.SelectedItems.Count == 0) { MessageBox.Show("请先选择一个文件。", "提示"); return; }
        string name = _files.SelectedItems[0].Text;
        await RunAsync(async () => { await action(RequireClient(), name); await RefreshFilesAsync(); }, operation);
    }

    private async Task RefreshStatusAsync(bool force = false)
    {
        if (_client is null || _refreshing) return;
        _refreshing = true;
        try
        {
            DeviceStatus status = await _client.GetStatusAsync();
            _lux.Text = $"{status.Lux:F1} Lux";
            _mode.Text = status.Mode;
            string loopText = status.PlaybackLoop ? " [单曲循环]" : "";
            _playback.Text = (string.IsNullOrEmpty(status.PlaybackSource)
                ? status.Playback
                : $"{status.Playback}  ({status.PlaybackSource})") + loopText;
            _wifi.Text = status.WifiConnected ? $"已连接  {status.IpAddress}" : "未连接";
            _storage.Text = $"剩余 {FormatBytes(status.LittleFsFree)} / {FormatBytes(status.LittleFsTotal)}";
            _error.Text = string.IsNullOrEmpty(status.LastError) ? "无" : status.LastError;
        }
        catch (Exception ex)
        {
            if (force) throw;
            _statusTimer.Stop();
            _connection.Text = "连接已中断";
            _connection.ForeColor = Color.Firebrick;
            Log($"状态刷新失败：{ex.Message}", true);
        }
        finally { _refreshing = false; }
    }

    private async Task RunAsync(Func<Task> action, string operation)
    {
        try { await action(); Log($"{operation}成功。", false); }
        catch (Exception ex)
        {
            Log($"{operation}失败：{ex.Message}", true);
            MessageBox.Show(ex.Message, $"{operation}失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void Log(string message, bool error) =>
        _log.AppendText($"[{DateTime.Now:HH:mm:ss}] {(error ? "错误 " : "")}{message}{Environment.NewLine}");

    private static string FormatBytes(long bytes)
    {
        string[] units = ["B", "KB", "MB", "GB"];
        double value = bytes;
        int unit = 0;
        while (value >= 1024 && unit < units.Length - 1) { value /= 1024; ++unit; }
        return $"{value:0.##} {units[unit]}";
    }

    private static string FormatTransfer(TransferProgress p)
    {
        string speed = p.Elapsed.TotalSeconds >= 1 ? FormatBytes((long)p.BytesPerSecond) + "/s" : "…";
        string eta = p.BytesPerSecond > 0 && p.Sent < p.Total
            ? $" · 剩余约 {TimeSpan.FromSeconds((p.Total - p.Sent) / p.BytesPerSecond):mm\\:ss}"
            : "";
        return $"{FormatBytes(p.Sent)} / {FormatBytes(p.Total)} · {speed} · 已用 {p.Elapsed:mm\\:ss}{eta}";
    }
}
