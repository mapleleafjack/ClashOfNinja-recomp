<#
.SYNOPSIS
    Builds SDL2 statically into the prefix the launcher and the game both look for.

.DESCRIPTION
    Windows has no prebuilt SDL2 for every target, so gcn-recomp builds it from source
    once per compiler under %LOCALAPPDATA%\gcn-recomp\SDL2-<version>-<tag>, and every
    project's CMake finds it there by that name. Building the launcher on its own needs
    that prefix to exist first; this script produces it without configuring the game,
    which is what CI does. A 'built' marker file records the version, so a second run
    does nothing unless -Force is given.

.EXAMPLE
    .\build-sdl2.ps1

.EXAMPLE
    .\build-sdl2.ps1 -Version 2.30.9 -Tag mingw64 -Force
#>
param(
    [string]$Version = '2.30.9',
    [string]$Tag = 'mingw64',
    [string]$Prefix = '',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# The tarball and hash gcn-recomp's CMakeLists.txt pins, so a prefix built here and one
# built by a game configure are interchangeable.
$sha256 = '24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4'
$url = "https://github.com/libsdl-org/SDL/releases/download/release-$Version/SDL2-$Version.tar.gz"

if (-not $Prefix) {
    if (-not $env:LOCALAPPDATA) { throw 'LOCALAPPDATA is not set; pass -Prefix.' }
    $Prefix = Join-Path $env:LOCALAPPDATA "gcn-recomp\SDL2-$Version-$Tag"
}

if ((Test-Path (Join-Path $Prefix 'built')) -and -not $Force) {
    Write-Host "SDL2 $Version is already built at $Prefix"
    exit 0
}

$work = "$Prefix-work"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
if (Test-Path $Prefix) { Remove-Item $Prefix -Recurse -Force }
New-Item -ItemType Directory -Force -Path $work | Out-Null

$tarball = Join-Path $work "SDL2-$Version.tar.gz"
Write-Host "downloading SDL2 $Version"
Invoke-WebRequest -Uri $url -OutFile $tarball
$actual = (Get-FileHash $tarball -Algorithm SHA256).Hash.ToLower()
if ($actual -ne $sha256) { throw "SDL2 $Version tarball hash mismatch: got $actual" }

Write-Host 'extracting'
# `tar` resolves to Windows' own tar.exe and to the system tar elsewhere, so this script
# also runs outside Windows, where its logic can be checked.
& tar -xzf $tarball -C $work
if ($LASTEXITCODE -ne 0) { throw "tar failed ($LASTEXITCODE)" }

# Name the compiler explicitly when one is on PATH: SDL2's configure would otherwise
# guess, and the launcher will later be built by this same compiler.
$compilers = @()
foreach ($pair in @(@('gcc.exe', 'CMAKE_C_COMPILER'), @('g++.exe', 'CMAKE_CXX_COMPILER'))) {
    $tool = Get-Command $pair[0] -ErrorAction SilentlyContinue
    if ($tool) { $compilers += "-D$($pair[1])=$($tool.Source)" }
}

Write-Host "configuring SDL2 into $Prefix"
& cmake -S (Join-Path $work "SDL2-$Version") -B (Join-Path $work 'build') -G Ninja `
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$Prefix" `
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF @compilers
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }

Write-Host 'building and installing'
& cmake --build (Join-Path $work 'build') --target install
if ($LASTEXITCODE -ne 0) { throw "SDL2 build failed ($LASTEXITCODE)" }

Set-Content -Path (Join-Path $Prefix 'built') -Value $Version
Remove-Item $work -Recurse -Force
Write-Host "SDL2 $Version is at $Prefix"
