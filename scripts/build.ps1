param()
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$toolchain = 'C:/msys64/ucrt64/bin'
if(Test-Path "$toolchain/g++.exe") { $env:PATH = "$toolchain;" + $env:PATH }
New-Item -ItemType Directory -Force build | Out-Null
$boost = 'third_party/boost_1_85_0'
$sqlite = 'third_party/sqlite-amalgamation-3460000'
if(-not (Test-Path "$boost/boost/asio.hpp") -or -not (Test-Path "$sqlite/sqlite3.c")) { throw 'Dependencies missing. Run scripts/setup.ps1 first.' }
& gcc -O2 -DSQLITE_THREADSAFE=1 -c "$sqlite/sqlite3.c" -o build/sqlite3.o
if($LASTEXITCODE -ne 0){throw 'SQLite build failed'}
$flags = @('-std=c++17','-O1','-Wall','-Wextra','-Wpedantic','-DBOOST_ERROR_CODE_HEADER_ONLY','-D_WIN32_WINNT=0x0601','-DNOMINMAX','-Iinclude',"-I$boost","-I$sqlite")
& g++ @flags -c src/core.cpp -o build/core.o
if($LASTEXITCODE -ne 0){throw 'Core build failed'}
foreach($target in @(@('telemetry_server','src/server.cpp','src/network.cpp'),@('device_simulator','src/simulator.cpp'),@('core_tests','tests/core_tests.cpp'))) {
    $name=$target[0];$sources=$target[1..($target.Count-1)]
    & g++ @flags @sources build/core.o build/sqlite3.o -lws2_32 -lmswsock -o "build/$name.exe"
    if($LASTEXITCODE -ne 0){throw "Build failed: $name"}
}
& ./build/core_tests.exe
if($LASTEXITCODE -ne 0){throw 'Tests failed'}
Write-Host 'Build complete.'
