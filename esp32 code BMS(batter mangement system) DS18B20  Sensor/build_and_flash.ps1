param(
  [string]$ProjectPath = (Get-Location).Path,
  [string]$Env = "esp32dev",
  [string]$UploadPort = "",
  [switch]$UseTemp
)

function Run-PlatformIO([string]$args) {
  $p = "platformio $args"
  Write-Host "> $p"
  & platformio $args
  return $LASTEXITCODE
}

if ($UseTemp) {
  $temp = Join-Path $env:TEMP ("bms_build_temp_{0}" -f ([guid]::NewGuid().ToString()))
  New-Item -ItemType Directory -Force -Path $temp | Out-Null
  Write-Host "Copying project to temp: $temp"
  robocopy "$ProjectPath" "$temp" platformio.ini src /E /NJH /NJS /NC /NS /NP | Out-Null
  Set-Location $temp
} else {
  Set-Location $ProjectPath
}

Write-Host "Building environment: $Env"
if (Run-PlatformIO "run --environment $Env") { Write-Error "Build failed"; exit 1 }

if ($UploadPort) {
  Write-Host "Uploading to port: $UploadPort"
  if (Run-PlatformIO "run --environment $Env --target upload --upload-port $UploadPort") { Write-Error "Upload failed"; exit 2 }
} else {
  Write-Host "Uploading (default port)"
  if (Run-PlatformIO "run --environment $Env --target upload") { Write-Error "Upload failed"; exit 3 }
}

Write-Host "Done."
