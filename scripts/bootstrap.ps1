$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $PSScriptRoot
$ToolDir = Join-Path $ProjectDir "build/tooling"
$QtMirror = if ($env:WT_QT_MIRROR) {
    $env:WT_QT_MIRROR
} else {
    "https://ftp.fau.de/qtproject/"
}
$QtMirrorUri = [Uri]$QtMirror
if (-not $QtMirrorUri.IsAbsoluteUri -or $QtMirrorUri.Scheme -ne "https") {
    throw "WT_QT_MIRROR must be an absolute HTTPS URL."
}

python -m venv $ToolDir
if ($LASTEXITCODE -ne 0) { throw "Python environment creation failed." }
& "$ToolDir/Scripts/python.exe" -m pip install `
    "aqtinstall==3.3.0" `
    "conan==2.19.1" `
    "cmake==4.1.2" `
    "ninja==1.13.0"
if ($LASTEXITCODE -ne 0) { throw "Build tool installation failed." }

& "$ToolDir/Scripts/aqt.exe" install-qt `
    windows desktop 6.11.1 win64_msvc2022_64 `
    --modules qtwebview `
    --base $QtMirrorUri.AbsoluteUri `
    -O "$ProjectDir/build/qt"
if ($LASTEXITCODE -ne 0) { throw "Qt installation failed." }

Write-Host "Tools installed under $ToolDir"
Write-Host "Qt installed under $ProjectDir/build/qt/6.11.1"
Write-Host "Activate the tools with: & build/tooling/Scripts/Activate.ps1"
