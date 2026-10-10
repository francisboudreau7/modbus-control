# Regenerates the application icon images from assets\icon\logo.svg (48 px and up) and
# assets\icon\logo-small.svg (32 px and below):
#   assets\icon\png\logo-<size>.png   (Qt window icon, via src\app\icons.qrc)
#   assets\icon\modbus_control.ico    (exe and installer icon)
# Needs a configured msvc-debug build (it builds the render_icon helper there).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_icons.ps1

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo 'build\msvc-debug'
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'

cmd /c "`"$vcvars`" >nul && cmake --build `"$build`" --target render_icon"
if ($LASTEXITCODE -ne 0) { throw 'building render_icon failed' }

$qt = 'C:\Qt\6.10.3\msvc2022_64'
$env:PATH = "$qt\bin;$build\bin;$env:PATH"
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = "$env:WINDIR\Fonts"
$env:QT_PLUGIN_PATH = "$qt\plugins"

$out = Join-Path $repo 'assets\icon\png'
New-Item -ItemType Directory -Force $out | Out-Null
$pngs = @()
foreach ($size in 16, 24, 32, 48, 64, 128, 256) {
    $svg = if ($size -le 32) { 'logo-small.svg' } else { 'logo.svg' }
    $png = Join-Path $out "logo-$size.png"
    & (Join-Path $build 'bin\render_icon.exe') (Join-Path $repo "assets\icon\$svg") $png $size
    if ($LASTEXITCODE -ne 0) { throw "rendering $size px failed" }
    $pngs += $png
}
python (Join-Path $PSScriptRoot 'pack_ico.py') (Join-Path $repo 'assets\icon\modbus_control.ico') @pngs
if ($LASTEXITCODE -ne 0) { throw 'packing the .ico failed' }
Write-Host 'Icons written to assets\icon'
