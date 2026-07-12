# build_stress.ps1 - compile + run the headless Pono UI lifecycle/leak soak (MinGW).
#
# Sibling to build_headless.ps1: the renderer proves a screen DRAWS once, this
# proves the screens survive being built, driven, and torn down thousands of
# times with the top-layer overlays churning, and that used-heap stays flat.
# Targets the historically-fragile paths: idle<->printing<->paused model swaps,
# overlay show/hide churn, the boot lifecycle, and the E-STOP top layer.
#
#   pwsh sim/build_stress.ps1 [-Iters 2000] [-Rebuild]
#
# Exit 0 + "STRESS CLEAN" = pass. An LVGL assert aborts (nonzero); a rising
# used-heap across iterations is a leak.
param(
  [int]$Iters = 2000,
  [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'

$bin  = 'C:\Strawberry\c\bin'
$gcc  = Join-Path $bin 'gcc.exe'
$gpp  = Join-Path $bin 'g++.exe'
$ar   = Join-Path $bin 'ar.exe'
$root = Split-Path $PSScriptRoot -Parent
$sim  = Join-Path $root 'sim'
$obj  = Join-Path $sim '_obj'
New-Item -ItemType Directory -Force $obj | Out-Null

$inc = @("-I$root", "-I$root\src", "-I$root\lvgl", '-DLV_CONF_INCLUDE_SIMPLE')
$cflags   = @('-std=c11',   '-O2', '-Wno-unused-parameter', '-Wno-unused-variable')
$cxxflags = @('-std=c++17', '-O2', '-Wno-unused-parameter')

# 1) LVGL core -> liblvgl.a (one-time, cached)
$lib = Join-Path $obj 'liblvgl.a'
if ($Rebuild -and (Test-Path $lib)) { Remove-Item $lib -Force }
if (-not (Test-Path $lib)) {
  Write-Host '[1/3] Compiling LVGL core (one-time, ~1-2 min)...'
  $lvglsrc = Get-ChildItem "$root\lvgl\src" -Recurse -Filter *.c |
    Where-Object { $_.FullName -notmatch '\\extra\\libs\\' } |
    ForEach-Object { $_.FullName }
  Push-Location $obj
  try {
    & $gcc -c @cflags @inc $lvglsrc
    if ($LASTEXITCODE -ne 0) { throw "LVGL compile failed ($LASTEXITCODE)" }
    $os = Get-ChildItem "$obj\*.o" | ForEach-Object { $_.FullName }
    & $ar rcs $lib $os
    if ($LASTEXITCODE -ne 0) { throw "ar failed ($LASTEXITCODE)" }
  } finally { Pop-Location }
  Write-Host "      liblvgl.a ready ($((Get-ChildItem $lib).Length) bytes)"
} else {
  Write-Host '[1/3] liblvgl.a cached (use -Rebuild to force)'
}

# 2) Pono fonts + anim frames (C, designated initializers -> gcc not g++)
Write-Host '[2/3] Compiling Pono fonts + anim frames...'
$fontobj = @()
foreach ($f in (Get-ChildItem "$root\assets\pono\fonts\*.c")) {
  $o = Join-Path $obj ("font_" + $f.BaseName + ".o")
  & $gcc -c @cflags @inc $f.FullName -o $o
  if ($LASTEXITCODE -ne 0) { throw "font $($f.Name) failed" }
  $fontobj += $o
}
$animobj = @()
if (Test-Path "$root\assets\pono\anim") {
  foreach ($f in (Get-ChildItem "$root\assets\pono\anim\*.c" -ErrorAction SilentlyContinue)) {
    $o = Join-Path $obj ("anim_" + $f.BaseName + ".o")
    & $gcc -c @cflags @inc $f.FullName -o $o
    if ($LASTEXITCODE -ne 0) { throw "anim $($f.Name) failed" }
    $animobj += $o
  }
}

# 3) builders + stress harness -> exe
Write-Host '[3/3] Compiling + linking stress harness...'
$exe = Join-Path $sim 'pono-stress.exe'
& $gpp @cxxflags @inc `
  (Join-Path $sim 'pono_stress.cpp') `
  (Join-Path $root 'src\pono_theme.cpp') `
  (Join-Path $root 'src\pono_home.cpp') `
  (Join-Path $root 'src\pono_anim.cpp') `
  $fontobj $animobj $lib -o $exe -lm -lpsapi
if ($LASTEXITCODE -ne 0) { throw "link failed ($LASTEXITCODE)" }
Write-Host "      built $exe"

# soak
Write-Host "Running $Iters-iteration soak..."
& $exe $Iters
$rc = $LASTEXITCODE
if ($rc -ne 0) { throw "STRESS FAILED (exit $rc)" }
Write-Host "STRESS OK (exit 0)"
