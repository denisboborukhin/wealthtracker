$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $PSScriptRoot
$ToolDir = Join-Path $ProjectDir "build/tooling"

python -m venv $ToolDir
& "$ToolDir/Scripts/python.exe" -m pip install `
    "conan==2.19.1" `
    "cmake==4.1.2" `
    "ninja==1.13.0"

Write-Host "Tools installed under $ToolDir"

