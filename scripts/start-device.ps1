param([string]$Device='sensor-001',[int]$Count=0,[string]$HostAddress='127.0.0.1',[int]$Port=9000)
$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$bin='C:/msys64/ucrt64/bin'
if(Test-Path $bin){$env:PATH="$bin;"+$env:PATH}
& ./build/device_simulator.exe $HostAddress $Port $Device $Count
