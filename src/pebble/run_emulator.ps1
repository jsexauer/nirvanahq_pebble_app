param (
    [string]$Emulator = "basalt"
)

Write-Host "Starting Pebble emulator ($Emulator) and streaming logs..."
wsl --cd $PSScriptRoot ~/.local/bin/pebble install --emulator $Emulator --logs
