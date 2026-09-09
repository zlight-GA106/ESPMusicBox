# 推送到 GitHub（会弹出 Git Credential Manager 登录窗口，请按提示完成授权）
# 用法: powershell -ExecutionPolicy Bypass -File scripts/push_github.ps1
$ErrorActionPreference = "Continue"
Set-Location "D:\opencode project\ESPMusicBox"
Write-Host "=== git push origin main ===" -ForegroundColor Cyan
git push origin main
Write-Host "=== exit=$LASTEXITCODE ===" -ForegroundColor Cyan
Read-Host "按回车关闭窗口"
