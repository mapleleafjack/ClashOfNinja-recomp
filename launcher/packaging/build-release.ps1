<#
.SYNOPSIS
    Builds NarutoLauncher.exe and assembles a release folder with the tools the first
    build needs.

.DESCRIPTION
    The launcher finds cmake, ninja, the C++ compiler and Python beside itself in
    tools\, or on PATH. A release made by this script carries portable copies of all
    four, so a player can extract their own disc and build the game without installing
    a toolchain. That is a large folder (about 1 GB unzipped, mostly MinGW); pass
    -Lean for a small launcher that uses an installed toolchain instead.

.EXAMPLE
    .\build-release.ps1 -Zip
    .\build-release.ps1 -Lean -Output ..\dist
    .\build-release.ps1 -Zip -Version 1.0.0
#>
param(
    [string]$Output = "",
    [switch]$Lean,
    [switch]$Zip,
    [switch]$SkipBuild,
    [switch]$Clean,
    [string]$Version = "",
    [string]$NodtoolVersion = 'v2.0.0-alpha.12'
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$launcher = Split-Path $here -Parent
$project = Split-Path $launcher -Parent

if (-not $Output) { $Output = Join-Path $launcher 'dist' }
$buildDir = Join-Path $launcher 'build-release'
$appName = 'NarutoLauncher'
$outDir = Join-Path $Output $appName

function Find-PackageTool([string]$name, [string]$packageFilter) {
    $packages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if ($packageFilter -and (Test-Path $packages)) {
        $package = Get-ChildItem $packages -Directory -Filter $packageFilter -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($package) {
            $found = Get-ChildItem $package.FullName -Recurse -Filter $name -File -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($found) { return $found.FullName }
        }
    }
    return $null
}

function Find-Tool([string]$name) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return $null
}

# Prefer the dedicated CMake and Ninja packages: MinGW ships its own copies, and its
# CMake only works with MinGW's share directory, which a release does not carry twice.
$cmake = Find-PackageTool 'cmake.exe' 'Kitware.CMake_*'
if (-not $cmake) { $cmake = Find-Tool 'cmake.exe' }
$ninja = Find-PackageTool 'ninja.exe' 'Ninja-build.Ninja_*'
if (-not $ninja) { $ninja = Find-Tool 'ninja.exe' }
$gxx = Find-PackageTool 'g++.exe' 'BrechtSanders.WinLibs*'
if (-not $gxx) { $gxx = Find-PackageTool 'clang++.exe' 'LLVM*' }
if (-not $gxx) { $gxx = Find-Tool 'g++.exe' }
if (-not $cmake) { throw 'cmake was not found. Install it with: winget install Kitware.CMake' }
if (-not $ninja) { throw 'ninja was not found. Install it with: winget install Ninja-build.Ninja' }
if (-not $gxx) {
    throw 'No C++ compiler was found. Install MinGW-w64: winget install BrechtSanders.WinLibs.POSIX.UCRT'
}

Write-Host "cmake:    $cmake"
Write-Host "ninja:    $ninja"
Write-Host "compiler: $gxx"

# The launcher puts these directories on PATH for its children; the build here needs
# the same, otherwise CMake cannot find Ninja or the compiler.
$toolDirs = @($cmake, $ninja, $gxx) | ForEach-Object { Split-Path $_ -Parent } | Select-Object -Unique
$env:PATH = ($toolDirs -join ';') + ';' + $env:PATH

if (-not $SkipBuild) {
    if ($Clean -and (Test-Path $buildDir)) { Remove-Item $buildDir -Recurse -Force }
    Write-Host "`nConfiguring and building the launcher..."
    # A release names the version the bundle reports; a plain build keeps the default.
    $versionArg = @()
    if ($Version) { $versionArg = "-DNLA_VERSION=$Version" }
    & $cmake -S $launcher -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_CXX_COMPILER=$gxx" @versionArg
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
    & $ninja -C $buildDir NarutoLauncher
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
}

$exe = Join-Path $buildDir 'NarutoLauncher.exe'
if (-not (Test-Path $exe)) { throw "NarutoLauncher.exe was not built at $exe" }

if (Test-Path $outDir) { Remove-Item $outDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Copy-Item $exe (Join-Path $outDir 'NarutoLauncher.exe')

if (-not $Lean) {
    $tools = Join-Path $outDir 'tools'
    New-Item -ItemType Directory -Force -Path $tools | Out-Null

    # CMake: bin and its module tree (share) are what a configure needs.
    $cmakeRoot = Split-Path (Split-Path $cmake -Parent) -Parent
    Write-Host "copying CMake from $cmakeRoot"
    New-Item -ItemType Directory -Force -Path (Join-Path $tools 'cmake') | Out-Null
    Copy-Item (Join-Path $cmakeRoot 'bin') (Join-Path $tools 'cmake\bin') -Recurse
    Copy-Item (Join-Path $cmakeRoot 'share') (Join-Path $tools 'cmake\share') -Recurse

    # Ninja, standalone, so tools\ninja\ninja.exe is found even without MinGW.
    New-Item -ItemType Directory -Force -Path (Join-Path $tools 'ninja') | Out-Null
    Copy-Item $ninja (Join-Path $tools 'ninja\ninja.exe')

    # MinGW-w64: the compiler, its linker, headers and libraries.
    $mingwRoot = Split-Path (Split-Path $gxx -Parent) -Parent
    Write-Host "copying MinGW-w64 from $mingwRoot (about 900 MB)"
    $mingwOut = Join-Path $tools 'mingw64'
    New-Item -ItemType Directory -Force -Path $mingwOut | Out-Null
    foreach ($part in 'bin', 'lib', 'libexec', 'include', 'share', 'x86_64-w64-mingw32') {
        $source = Join-Path $mingwRoot $part
        if (Test-Path $source) { Copy-Item $source (Join-Path $mingwOut $part) -Recurse }
    }

    # Python: the recompiler's code generator needs an interpreter at build time.
    $pythonOut = Join-Path $tools 'python'
    if (-not (Test-Path (Join-Path $pythonOut 'python.exe'))) {
        $pythonVersion = '3.12.7'
        $pythonZip = Join-Path $env:TEMP "python-$pythonVersion-embed-amd64.zip"
        if (-not (Test-Path $pythonZip)) {
            Write-Host "downloading Python $pythonVersion (embeddable)"
            $url = "https://www.python.org/ftp/python/$pythonVersion/python-$pythonVersion-embed-amd64.zip"
            Invoke-WebRequest -Uri $url -OutFile $pythonZip
        }
        Expand-Archive -Path $pythonZip -DestinationPath $pythonOut -Force
    }

    # SDL2: a prefix built once by gcn-recomp's CMake. Copying it keeps the player's
    # first build offline and skips compiling SDL2 on their machine.
    $sdlParent = Join-Path $env:LOCALAPPDATA 'gcn-recomp'
    if (Test-Path $sdlParent) {
        $sdl = Get-ChildItem $sdlParent -Directory -Filter 'SDL2-*' |
            Where-Object { $_.Name -notlike '*-work' } | Select-Object -First 1
        if ($sdl) {
            Write-Host "copying SDL2 from $($sdl.FullName)"
            Copy-Item $sdl.FullName (Join-Path $tools 'sdl2') -Recurse
        } else {
            Write-Warning 'No built SDL2 prefix was found; the first build will download and compile SDL2.'
        }
    }

    # nodtool: reads the image formats the launcher itself does not (RVZ, WIA, GCZ,
    # WBFS, TGC, NFS). A local copy wins; otherwise take Khronos' published Windows
    # binary for the pinned release.
    $nodOut = Join-Path $tools 'nodtool'
    New-Item -ItemType Directory -Force -Path $nodOut | Out-Null
    $nodtool = Find-Tool 'nodtool.exe'
    if (-not $nodtool) {
        $nodtool = Join-Path $outDir 'tools\nodtool\nodtool.exe'
        if (-not (Test-Path $nodtool)) {
            Write-Host "downloading nodtool $NodtoolVersion"
            $url = "https://github.com/encounter/nod/releases/download/$NodtoolVersion/nodtool-windows-x86_64.exe"
            Invoke-WebRequest -Uri $url -OutFile $nodtool
        }
    } else {
        Copy-Item $nodtool (Join-Path $nodOut 'nodtool.exe')
    }
    $nodLicense = Join-Path $nodOut 'LICENSE.txt'
    try {
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/encounter/nod/$NodtoolVersion/LICENSE" -OutFile $nodLicense
    } catch {
        $apache = Join-Path $project 'licenses\Apache-2.0.txt'
        if (Test-Path $apache) { Copy-Item $apache $nodLicense }
    }
}

$readme = @'
Naruto: Clash of Ninja - launcher
=================================

NarutoLauncher.exe extracts your own disc image, builds the recompilation and
starts the game. Nothing from the game is included here.

First start
    Put this folder beside the project (the folder holding naruto\ and tools\),
    or use "Locate project folder" to point at it once. Keep the folder path
    free of spaces: the bundled MinGW-w64 compiler cannot run from one.

    Choose Disc Image... and pick your own USA copy of the game. The launcher
    extracts it, then configures and compiles the game with the tools in tools\.
    Later starts go straight to Play.

Tools
    A full release carries cmake, ninja, MinGW-w64, Python and nodtool in tools\; the
    launcher prefers them over anything installed on the machine. nodtool reads RVZ,
    WIA, GCZ, WBFS, TGC and NFS images, so any of those can be chosen directly.
    A -Lean release has no tools and uses PATH or a system install instead.

Settings
    The Settings panel remembers the project, the last image, window scale,
    controller input logging, and can create Desktop or Start menu shortcuts.

This launcher writes nothing into the game: the extracted disc stays in
naruto\rom\disc, saves in naruto\saves, and controls in naruto\controls.txt.
'@
Set-Content -Path (Join-Path $outDir 'README.txt') -Value $readme -Encoding UTF8

foreach ($notice in 'THIRD-PARTY-NOTICES.md', 'LICENSE') {
    $source = Join-Path $project $notice
    if (Test-Path $source) { Copy-Item $source (Join-Path $outDir $notice) }
}
$licenses = Join-Path $project 'licenses'
if (Test-Path $licenses) { Copy-Item $licenses (Join-Path $outDir 'licenses') -Recurse }

$size = (Get-ChildItem $outDir -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("`nBuilt {0} ({1:N0} MB)" -f $outDir, ($size / 1MB))

if ($Zip) {
    $zipName = if ($Version) { "NarutoLauncher-Windows-x64-v$Version.zip" } else { 'NarutoLauncher.zip' }
    $zipPath = Join-Path $Output $zipName
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Write-Host "compressing to $zipPath (this takes a while)"
    Compress-Archive -Path (Join-Path $outDir '*') -DestinationPath $zipPath
    Write-Host ("Zip: {0:N0} MB" -f ((Get-Item $zipPath).Length / 1MB))
}
