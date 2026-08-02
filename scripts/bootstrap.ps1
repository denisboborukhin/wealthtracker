$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $PSScriptRoot
$ToolDir = Join-Path $ProjectDir "build/tooling"

python -m venv $ToolDir
& "$ToolDir/Scripts/python.exe" -m pip install `
    "aqtinstall==3.3.0" `
    "conan==2.19.1" `
    "cmake==4.1.2" `
    "ninja==1.13.0"

& "$ToolDir/Scripts/aqt.exe" install-qt `
    windows desktop 6.11.1 win64_msvc2022_64 `
    -m qtpositioning qtwebchannel qtwebengine `
    -O "$ProjectDir/build/qt"

Write-Host "Tools installed under $ToolDir"
Write-Host "Qt installed under $ProjectDir/build/qt/6.11.1"
Write-Host "Activate the tools with: & build/tooling/Scripts/Activate.ps1"
