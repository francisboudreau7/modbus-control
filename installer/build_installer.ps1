# Builds Modbus Control and the Windows installer (Inno Setup 6).
#
# Usage (from the repository root):
#   powershell -NoProfile -ExecutionPolicy Bypass -File installer\build_installer.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File installer\build_installer.ps1 -Preset msvc-debug -Version 0.1.1
#
# Output:
#   out\build      CMake build tree
#   out\install    installed runnable folder (cmake --install)
#   out\installer  ModbusControl-<version>-setup.exe

param(
    [string]$Preset = 'msvc-release',
    [string]$Version = ''
)

$ErrorActionPreference = 'Stop'

function Fail([string]$Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

# Paths
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$out = Join-Path $repo 'out'
$buildDir = Join-Path $out 'build'
$installDir = Join-Path $out 'install'
$installerDir = Join-Path $out 'installer'
$issFile = Join-Path $PSScriptRoot 'modbus_control.iss'

if (-not (Test-Path (Join-Path $repo 'CMakeLists.txt'))) { Fail "CMakeLists.txt not found in $repo" }
if (-not (Test-Path $issFile)) { Fail "Inno Setup script not found: $issFile" }

# Version from the project() call
if ([string]::IsNullOrEmpty($Version)) {
    $cmakeText = Get-Content -Raw -Path (Join-Path $repo 'CMakeLists.txt')
    $m = [regex]::Match($cmakeText, 'project\(\s*ModbusControl\s+VERSION\s+(\d+\.\d+\.\d+)')
    if (-not $m.Success) { Fail 'Could not read the version from project(ModbusControl VERSION x.y.z) in CMakeLists.txt' }
    $Version = $m.Groups[1].Value
}
Write-Host "Preset:  $Preset"
Write-Host "Version: $Version"

# Locate vcvars64.bat (vswhere first, then the VS 2022 Build Tools default path)
$vcvars = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) {
        $candidate = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path $candidate) { $vcvars = $candidate }
    }
}
if (-not $vcvars) {
    $candidate = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    if (Test-Path $candidate) { $vcvars = $candidate }
}
if (-not $vcvars) { Fail 'vcvars64.bat not found. Install Visual Studio 2022 Build Tools with the C++ workload.' }
Write-Host "vcvars:  $vcvars"

# Configure, build and install inside a cmd session with the MSVC environment.
# A generated .cmd file avoids PowerShell quoting problems with cmd /c.
New-Item -ItemType Directory -Force -Path $out | Out-Null
$batch = Join-Path $out 'build_installer_env.cmd'
$lines = @(
    '@echo off',
    "call `"$vcvars`" >nul",
    'if errorlevel 1 exit /b 10',
    "cmake --preset $Preset -B `"$buildDir`"",
    'if errorlevel 1 exit /b 11',
    "cmake --build `"$buildDir`"",
    'if errorlevel 1 exit /b 12',
    "cmake --install `"$buildDir`" --prefix `"$installDir`"",
    'if errorlevel 1 exit /b 13'
)
Set-Content -Path $batch -Value $lines -Encoding Ascii

Write-Host 'Configuring, building and installing (the first configure takes several minutes)...'
& cmd.exe /c "`"$batch`""
if ($LASTEXITCODE -ne 0) {
    $step = @{ 10 = 'loading vcvars64'; 11 = 'configuring'; 12 = 'building'; 13 = 'installing' }[$LASTEXITCODE]
    if (-not $step) { $step = 'building' }
    Fail "CMake failed while $step (exit code $LASTEXITCODE)."
}

if (-not (Test-Path (Join-Path $installDir 'bin\modbus_control.exe'))) {
    Fail "Install step finished but bin\modbus_control.exe is missing under $installDir"
}

# Locate ISCC.exe
$iscc = $null
$cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($cmd) { $iscc = $cmd.Source }
if (-not $iscc) {
    $candidate = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
    if (Test-Path $candidate) { $iscc = $candidate }
}
if (-not $iscc) {
    $candidate = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
    if (Test-Path $candidate) { $iscc = $candidate }
}
if (-not $iscc) { Fail 'ISCC.exe (Inno Setup 6) not found. Install it from https://jrsoftware.org/isinfo.php' }
Write-Host "ISCC:    $iscc"

# Compile the installer
New-Item -ItemType Directory -Force -Path $installerDir | Out-Null
$setupExe = Join-Path $installerDir "ModbusControl-$Version-setup.exe"
if (Test-Path $setupExe) { Remove-Item -Force $setupExe }

& $iscc "/DAppVersion=$Version" "/DSourceDir=$installDir" "/DRepoDir=$repo" "/O$installerDir" $issFile
if ($LASTEXITCODE -ne 0) { Fail "ISCC failed (exit code $LASTEXITCODE)." }
if (-not (Test-Path $setupExe)) { Fail "ISCC succeeded but $setupExe was not created." }

$sizeMB = [math]::Round((Get-Item $setupExe).Length / 1MB, 2)
Write-Host ''
Write-Host "Installer created: $setupExe ($sizeMB MB)" -ForegroundColor Green
exit 0
