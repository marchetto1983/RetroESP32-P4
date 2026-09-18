# build_quick.ps1 — copia di build_all.ps1 ridotta a launcher + nes
$ErrorActionPreference = "Stop"
$ROOT = $PSScriptRoot
. (Join-Path $ROOT 'tools\Resolve-IdfEnv.ps1')
Initialize-IdfEnv

$BINS = Join-Path $ROOT 'firmware'
New-Item -ItemType Directory -Path $BINS -Force | Out-Null

Write-Host "`n=== Building Launcher ===" -ForegroundColor Cyan
Push-Location "$ROOT\launcher"
if (Test-Path "build") { Remove-Item -Recurse -Force "build" }
Remove-Item -Force "sdkconfig" -ErrorAction SilentlyContinue
idf.py build
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "Launcher build failed" }
Copy-Item "build\launcher.bin" "$BINS\launcher.bin" -Force
Copy-Item "build\bootloader\bootloader.bin" "$BINS\bootloader.bin" -Force
Copy-Item "build\partition_table\partition-table.bin" "$BINS\partition-table.bin" -Force
Copy-Item "build\ota_data_initial.bin" "$BINS\ota_data_initial.bin" -Force
Pop-Location
Write-Host "Launcher: OK" -ForegroundColor Green

$apps = @(
    @{ Name = "nes"; Dir = "apps\nes"; Bin = "nes_app.bin" }
)

foreach ($app in $apps) {
    Write-Host "`n=== Building $($app.Name) ===" -ForegroundColor Cyan
    Push-Location "$ROOT\$($app.Dir)"
    if (Test-Path "build") { Remove-Item -Recurse -Force "build" }
    Remove-Item -Force "sdkconfig" -ErrorAction SilentlyContinue
    idf.py build
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw "$($app.Name) build failed" }
    Copy-Item "build\$($app.Bin)" "$BINS\$($app.Bin)" -Force
    Pop-Location
    Write-Host "$($app.Name): OK" -ForegroundColor Green
}

Write-Host "`n=== All builds complete! ===" -ForegroundColor Green
& "$ROOT\generate_merged_bin.ps1"