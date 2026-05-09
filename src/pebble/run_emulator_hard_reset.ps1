param (
    [string]$Emulator = "emery"
)

Write-Host "Killing any existing Pebble emulator background services..."
wsl ~/.local/bin/pebble kill
Get-Process -Name "qemu-system-arm", "pebble" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

Write-Host "Wiping Pebble emulator state..."
wsl --cd $PSScriptRoot ~/.local/bin/pebble wipe

Write-Host "Cleaning Pebble build directory..."
wsl --cd $PSScriptRoot ~/.local/bin/pebble clean

Write-Host "Building Pebble app..."
wsl --cd $PSScriptRoot ~/.local/bin/pebble build

# After a wipe, the emulator needs time to initialize before an install will succeed.
# Kick off the emulator, wait for boot, then retry the install.
Write-Host "Starting Pebble emulator ($Emulator) in background..."
Start-Process -FilePath "wsl" -ArgumentList "--cd `"$PSScriptRoot`" ~/.local/bin/pebble install --emulator $Emulator" -WindowStyle Hidden

Write-Host "Waiting 6 seconds for emulator to boot after hard reset..."
Start-Sleep -Seconds 6

# Retry install up to 3 times with a 3-second gap
$maxRetries = 3
$installed = $false
for ($i = 1; $i -le $maxRetries; $i++) {
    Write-Host "Install attempt $i of $maxRetries..."
    $result = wsl --cd $PSScriptRoot ~/.local/bin/pebble install --emulator $Emulator 2>&1
    if ($result -match "succeeded") {
        Write-Host "App installed successfully!"
        $installed = $true
        break
    }
    Write-Host "Not ready yet, retrying in 3 seconds..."
    Start-Sleep -Seconds 3
}

if (-not $installed) {
    Write-Host "Warning: Install may not have succeeded. Try running run_emulator.ps1 manually."
}

Write-Host "Streaming logs (Ctrl+C to stop)..."
wsl --cd $PSScriptRoot ~/.local/bin/pebble logs --emulator $Emulator
