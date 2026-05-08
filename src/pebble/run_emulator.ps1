param (
    [string]$Emulator = "emery"
)

Write-Host "Cleaning Pebble build directory..."
wsl --cd $PSScriptRoot ~/.local/bin/pebble clean

Write-Host "Building Pebble app..."
wsl --cd $PSScriptRoot ~/.local/bin/pebble build

Write-Host "Starting Pebble emulator ($Emulator) and streaming logs..."
wsl --cd $PSScriptRoot ~/.local/bin/pebble install --emulator $Emulator --logs
