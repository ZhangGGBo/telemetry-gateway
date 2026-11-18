$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$bin='C:/msys64/ucrt64/bin'
if(Test-Path $bin){$env:PATH="$bin;"+$env:PATH}
if(-not(Test-Path build/telemetry_server.exe)){& "$PSScriptRoot/build.ps1";if($LASTEXITCODE -ne 0){throw 'Build failed'}}
& ./build/telemetry_server.exe config/server.json
