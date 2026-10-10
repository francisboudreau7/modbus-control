# Line coverage of our sources (src\, server\) by the test suite, measured with OpenCppCoverage
# (https://github.com/OpenCppCoverage/OpenCppCoverage, installed in C:\Program Files\OpenCppCoverage).
# Runs every test executable of the msvc-debug build (and the app smoke test) under the tool,
# merges the results and writes:
#   build\coverage\html\index.html   per-file and per-line report
#   build\coverage\coverage.xml      Cobertura summary
# and prints the line coverage per source file. Build the msvc-debug preset first.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\coverage.ps1

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo 'build\msvc-debug'
$bin = Join-Path $build 'bin'
$out = Join-Path $build 'coverage'
$occ = 'C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe'
if (-not (Test-Path $occ)) { throw "OpenCppCoverage not found at $occ" }

# Same environment as ctest gives the tests.
$qt = 'C:\Qt\6.10.3\msvc2022_64'
$env:PATH = "$qt\bin;$build\vcpkg_installed\x64-windows\debug\bin;$bin;$env:PATH"
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = "$env:WINDIR\Fonts"
$env:QT_PLUGIN_PATH = "$qt\plugins"

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null

$common = @('--quiet', '--sources', (Join-Path $repo 'src'), '--sources', (Join-Path $repo 'server'),
            '--modules', $bin, '--cover_children')
$runs = @()
foreach ($exe in Get-ChildItem $bin -Filter 'tst_*.exe') {
    $runs += ,@($exe.BaseName, $exe.FullName, @())
}
$runs += ,@('app_smoke', (Join-Path $bin 'modbus_control.exe'), @('--smoke'))

$inputs = @()
foreach ($run in $runs) {
    $name, $exe, $extra = $run
    $cov = Join-Path $out "$name.cov"
    Write-Host "== $name"
    & $occ @common --export_type "binary:$cov" -- $exe @extra | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Warning "$name exited with $LASTEXITCODE" }
    $inputs += @('--input_coverage', $cov)
}

# Merge: one more (cheap) run whose report includes every input.
& $occ @common @inputs --export_type "html:$(Join-Path $out 'html')" `
    --export_type "cobertura:$(Join-Path $out 'coverage.xml')" -- (Join-Path $bin 'tst_smoke.exe') | Out-Null

[xml]$xml = Get-Content (Join-Path $out 'coverage.xml')
# A source file appears once per module (executable) that contains it: merge by file, a line is
# covered when any module ran it.
$files = @{}
foreach ($class in $xml.coverage.packages.package.classes.class) {
    $name = $class.filename -replace '^.*?modbus-control[\\/]', ''
    if (-not $files.ContainsKey($name)) { $files[$name] = @{} }
    foreach ($line in @($class.lines.line)) {
        $n = [int]$line.number
        $files[$name][$n] = ($files[$name][$n] -or ([int]$line.hits -gt 0))
    }
}
$rows = foreach ($name in $files.Keys) {
    $lines = $files[$name]
    $hit = @($lines.Values | Where-Object { $_ }).Count
    [pscustomobject]@{ File = $name; Lines = $lines.Count; Covered = $hit
                       Percent = if ($lines.Count) { [math]::Round(100.0 * $hit / $lines.Count, 1) } else { 0 } }
}
$rows | Where-Object { $_.File -like '*.cpp' } | Sort-Object Percent | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
$total = ($rows | Measure-Object Lines -Sum).Sum
$covered = ($rows | Measure-Object Covered -Sum).Sum
Write-Host ("Total (all files): {0} / {1} lines = {2:N1} %" -f $covered, $total, (100.0 * $covered / $total))
Write-Host "Report: $(Join-Path $out 'html\index.html')"
