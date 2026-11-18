$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force third_party | Out-Null
if(-not(Test-Path third_party/boost_1_85_0/boost/asio.hpp)) {
 Invoke-WebRequest https://archives.boost.io/release/1.85.0/source/boost_1_85_0.tar.gz -OutFile third_party/boost.tar.gz
 tar -xzf third_party/boost.tar.gz -C third_party boost_1_85_0/boost boost_1_85_0/LICENSE_1_0.txt
 if($LASTEXITCODE -ne 0){throw 'Boost extraction failed'}
}
if(-not(Test-Path third_party/sqlite-amalgamation-3460000/sqlite3.c)) {
 Invoke-WebRequest https://www.sqlite.org/2024/sqlite-amalgamation-3460000.zip -OutFile third_party/sqlite.zip
 Expand-Archive third_party/sqlite.zip third_party -Force
}
Write-Host 'Dependencies ready: Boost 1.85.0, SQLite 3.46.0'
