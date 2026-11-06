param([string]$Port)

$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot

if (-not $Port) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if (-not $ports) {
        throw "No COM port found. Plug in the display's USB-C cable, then run this again."
    }
    Write-Host "COM ports on this PC: $($ports -join ', ')"
    Write-Host ""
    Write-Host "Use the CH340C UART port. The native USB pins are shared with the touch"
    Write-Host "controller I2C bus (GPIO19/20) on this display."
    Write-Host ""
    $Port = Read-Host "Which one is the display?"
}

Write-Host "Building and flashing to $Port ..."
python -m platformio run -e viewe_5 -t upload --upload-port $Port

Write-Host ""
Write-Host "Done. Open the serial monitor with:"
Write-Host "  python -m platformio device monitor -e viewe_5"
Write-Host "(115200 baud; expected: Wi-Fi connect, IP, mDNS, then 'Client connected' when the PC client runs)"
