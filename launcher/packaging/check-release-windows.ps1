<#
.SYNOPSIS
    Checks a Windows release the way a player receives it: unzip the archive the release
    publishes, then confirm the folder inside is the one that runs.

.DESCRIPTION
    Everything is read from the extracted copy rather than the build tree: a file that only
    exists in the build tree, a launcher built for the wrong machine, or a zip whose name the
    update check would not recognise is exactly what a download would get wrong. CI runs this
    after packaging, on every run, so a release cannot ship a folder nobody started.

.EXAMPLE
    ./launcher/packaging/check-release-windows.ps1 `
        -Archive launcher/dist/NarutoLauncher-Windows-x64-v1.0.1.zip -Version 1.0.1

    # or a folder that has not been packed yet
    ./launcher/packaging/check-release-windows.ps1 `
        -Dir launcher/dist/NarutoLauncher -Version 0.0.0
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Position = 0)][string]$Archive,
    [string]$Dir
)

$ErrorActionPreference = 'Stop'

function Fail([string]$message) {
    [Console]::Error.WriteLine("error: $message")
    exit 1
}

function Say([string]$message) {
    Write-Host "  $message"
}

$usage = "usage: check-release-windows.ps1 -Version <version> <archive.zip>`n" +
         "       check-release-windows.ps1 -Version <version> -Dir <unpacked folder>"

if (-not $Version) { Fail "-Version is required`n$usage" }
if (-not $Archive -and -not $Dir) { Fail "pass the release zip, or -Dir for an unpacked folder`n$usage" }
if ($Archive -and $Dir) { Fail "pass the zip or -Dir, not both" }

$work = Join-Path ([IO.Path]::GetTempPath()) ("nla-check-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null

try {
    $stage = $null
    if ($Archive) {
        if (-not (Test-Path $Archive -PathType Leaf)) { Fail "no archive at $Archive" }

        # update.cpp picks the asset to download by name: the platform, the version and the
        # architecture have to be in it, and it has to end in the type this platform unpacks.
        $wantName = "NarutoLauncher-Windows-x64-v$Version.zip"
        $gotName = Split-Path $Archive -Leaf
        if ($gotName -ne $wantName) {
            Fail "the asset is named '$gotName', and an update is only offered for '$wantName'"
        }

        $stage = Join-Path $work 'unpacked'
        New-Item -ItemType Directory -Force -Path $stage | Out-Null
        # The tar Windows ships is bsdtar, which reads a zip, and it is what the packager
        # itself writes the archive with; Expand-Archive takes minutes on a full release.
        $tar = Get-Command tar.exe -ErrorAction SilentlyContinue
        if ($tar) {
            & $tar.Source -xf $Archive -C $stage
            if ($LASTEXITCODE -ne 0) { Fail "tar could not unpack $Archive ($LASTEXITCODE)" }
        } else {
            Expand-Archive -Path $Archive -DestinationPath $stage -Force
        }

        # The zip's root is the folder a player unzips, so the launcher sits at the top of it.
        foreach ($want in 'NarutoLauncher.exe', 'mods.json', 'README.txt', 'LICENSE',
                          'THIRD-PARTY-NOTICES.md', 'licenses', 'gcn-recomp\CMakeLists.txt',
                          'tools\extract_disc.py', 'tools\setup.sh') {
            if (-not (Test-Path (Join-Path $stage $want))) { Fail "the archive is missing $want" }
        }
        Say "the archive holds the launcher, the recompiler, the notices and the README"
    } else {
        $stage = $Dir
        if (-not (Test-Path $stage -PathType Container)) { Fail "no folder at $stage" }
    }

    $exe = Join-Path $stage 'NarutoLauncher.exe'
    if (-not (Test-Path $exe -PathType Leaf)) { Fail "the folder has no NarutoLauncher.exe" }

    # The PE headers, read directly: what the loader acts on, and nothing a checking machine
    # has to have installed to read.
    $bytes = [IO.File]::ReadAllBytes($exe)
    if ($bytes.Length -lt 0x100 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        Fail "NarutoLauncher.exe is not a Windows executable"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -le 0 -or $peOffset + 24 -gt $bytes.Length -or
        $bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45) {
        Fail "NarutoLauncher.exe has no PE header"
    }

    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    if ($machine -ne 0x8664) {
        Fail ("the launcher is built for machine 0x{0:X4}, and the release is x64 (0x8664)" -f $machine)
    }
    $optional = $peOffset + 24
    if ([BitConverter]::ToUInt16($bytes, $optional) -ne 0x20B) {
        Fail "the launcher is not a PE32+ image, so it is not the x64 build"
    }
    # A WIN32 (GUI) subsystem program opens a window and no console; a console build would
    # flash a terminal in front of a player on every start.
    $subsystem = [BitConverter]::ToUInt16($bytes, $optional + 68)
    if ($subsystem -ne 2) {
        Fail "the launcher starts as a console program (subsystem $subsystem), so a terminal would open with it"
    }

    $sectionCount = [BitConverter]::ToUInt16($bytes, $peOffset + 6)
    if ($sectionCount -lt 1) { Fail "NarutoLauncher.exe has no sections" }
    $sectionTable = $optional + [BitConverter]::ToUInt16($bytes, $peOffset + 20)
    $sections = foreach ($i in 0..($sectionCount - 1)) {
        $at = $sectionTable + $i * 40
        [pscustomobject]@{
            Address = [BitConverter]::ToUInt32($bytes, $at + 12)
            Size    = [Math]::Max([BitConverter]::ToUInt32($bytes, $at + 8),
                                  [BitConverter]::ToUInt32($bytes, $at + 16))
            Raw     = [BitConverter]::ToUInt32($bytes, $at + 20)
        }
    }

    function ConvertFrom-Rva([uint32]$rva) {
        foreach ($section in $sections) {
            if ($rva -ge $section.Address -and $rva -lt ($section.Address + $section.Size)) {
                return $rva - $section.Address + $section.Raw
            }
        }
        Fail ("the file has no section for RVA 0x{0:X}" -f $rva)
    }

    # The DLLs the loader brings in before main runs. SDL2 is linked statically, and a
    # launcher that imported it would need SDL2.dll beside it on a machine that has none.
    $imported = @()
    $importRva = [BitConverter]::ToUInt32($bytes, $optional + 112 + 8)
    if ($importRva -ne 0) {
        $at = ConvertFrom-Rva $importRva
        while ($true) {
            $nameRva = [BitConverter]::ToUInt32($bytes, $at + 12)
            $firstThunk = [BitConverter]::ToUInt32($bytes, $at + 16)
            if ($nameRva -eq 0 -and $firstThunk -eq 0) { break }
            $nameAt = ConvertFrom-Rva $nameRva
            $name = ''
            while ($bytes[$nameAt] -ne 0) {
                $name += [char]$bytes[$nameAt]
                $nameAt++
            }
            $imported += $name
            $at += 20
        }
    }
    $sdl = @($imported | Where-Object { $_ -like '*SDL*' })
    if ($sdl.Count -gt 0) {
        Fail "the launcher imports $($sdl -join ', '), so the download would need SDL2 beside it"
    }
    Say "x64, with SDL2 linked statically"

    # A GUI-subsystem program has no console, so $LASTEXITCODE never fills in: only
    # Start-Process -Wait -PassThru can say whether it worked.
    $runs = Join-Path $work 'runs'
    New-Item -ItemType Directory -Force -Path $runs | Out-Null

    function Invoke-Launcher([string[]]$arguments, [string]$log) {
        $quoted = foreach ($argument in $arguments) {
            if ($argument -match '\s') { '"' + $argument + '"' } else { $argument }
        }
        $process = Start-Process -FilePath $exe -ArgumentList ($quoted -join ' ') `
            -WorkingDirectory $runs -RedirectStandardOutput "$log.out" `
            -RedirectStandardError "$log.err" -Wait -PassThru
        return $process.ExitCode
    }

    function Show-Log([string]$log) {
        foreach ($path in "$log.out", "$log.err") {
            if (Test-Path $path) { Get-Content $path | ForEach-Object { [Console]::Error.WriteLine($_) } }
        }
    }

    # The version compiled into the launcher is what the update check compares against the
    # newest release; a folder built at the wrong one offers a player an update to itself.
    $versionLog = Join-Path $runs 'version'
    if ((Invoke-Launcher @('--version') $versionLog) -ne 0) {
        Show-Log $versionLog
        Fail "--version failed, so the launcher does not run from the folder"
    }
    $got = (Get-Content "$versionLog.out" -Raw).Trim()
    if ($got -ne $Version) {
        Fail "the launcher reports version '$got' and the release is '$Version', so its update check would compare the wrong version"
    }
    Say "the launcher reports version $got"

    # A player has no checkout to fall back on: without these the first click says the
    # recompiler is missing.
    foreach ($want in 'mods.json', 'gcn-recomp\CMakeLists.txt', 'tools\extract_disc.py', 'tools\setup.sh') {
        if (-not (Test-Path (Join-Path $stage $want))) { Fail "the release is missing $want" }
    }
    if (Test-Path (Join-Path $stage 'tools\cmake')) {
        # A full release: the toolchain is what makes the first build work offline. A -Lean
        # one fetches the pinned archive instead, which is what CI packages.
        foreach ($want in 'tools\cmake\bin\cmake.exe', 'tools\ninja\ninja.exe', 'tools\mingw64\bin\g++.exe') {
            if (-not (Test-Path (Join-Path $stage $want))) {
                Fail "the release carries a toolchain but not $want, which a first build needs"
            }
        }
        Say "the folder carries the toolchain"
    } else {
        Say "no toolchain in the folder: the launcher downloads the pinned tools archive on first use"
    }

    # Nothing derived from a disc may be published, and the folder is where a build tree's
    # leftovers would ride along: a copy of the launcher that has been run writes its
    # projects and its saves beside itself. covers\ is not in this list, unlike the other
    # platforms: this packager ships the box art a machine already had, and box art is not
    # derived from a disc.
    foreach ($bad in 'project', 'config', 'saves', 'naruto', 'naruto2', 'gnt3', 'gnt4', 'narutoeu') {
        if (Test-Path (Join-Path $stage $bad)) { Fail "the release must not carry $bad" }
    }
    $disc = @(Get-ChildItem -Path $stage -Recurse -File -ErrorAction SilentlyContinue | Where-Object {
        $_.Extension -in '.iso', '.ciso', '.rvz', '.wia', '.gcz', '.wbfs', '.tgc', '.nfs', '.dol', '.trk', '.h4m'
    })
    if ($disc.Count -gt 0) {
        Fail "the release carries a disc file: $($disc[0].FullName)"
    }
    Say "nothing derived from a disc is in the folder"

    # The launcher itself, from the extracted copy: a missing DLL, a resource it only found
    # in the build tree, or a binary that will not start shows up here and nowhere else.
    $project = Join-Path $runs 'project'
    if ((Invoke-Launcher @('--make-project', $project) (Join-Path $runs 'make-project')) -ne 0) {
        Show-Log (Join-Path $runs 'make-project')
        Fail "--make-project failed, so the launcher does not run from the folder"
    }
    foreach ($game in 'naruto', 'naruto2', 'gnt3', 'gnt4', 'narutoeu') {
        if (-not (Test-Path (Join-Path $project "$game\CMakeLists.txt"))) {
            Fail "the launcher did not write the $game project"
        }
    }
    if (-not (Test-Path (Join-Path $project 'gcn-recomp\CMakeLists.txt'))) {
        Fail "the launcher did not copy the recompiler into the project it wrote"
    }
    Say "--make-project wrote all five games and the recompiler"

    $frame = Join-Path $runs 'frame.bmp'
    if ((Invoke-Launcher @('--render', $frame, 'main') (Join-Path $runs 'render')) -ne 0) {
        Show-Log (Join-Path $runs 'render')
        Fail "--render failed, so the launcher cannot draw a frame"
    }
    if (-not (Test-Path $frame) -or (Get-Item $frame).Length -lt 54) { Fail "--render wrote no frame" }
    $bmp = [IO.File]::ReadAllBytes($frame)
    if ($bmp[0] -ne 0x42 -or $bmp[1] -ne 0x4D) { Fail "--render did not write a bitmap" }
    # Width and height, little-endian, at 18 and 22 bytes into a bitmap header. A frame that
    # is zero pixels across is the failure a render can report as success.
    $width = [BitConverter]::ToInt32($bmp, 18)
    $height = [BitConverter]::ToInt32($bmp, 22)
    if ($width -lt 320 -or $height -lt 240) {
        Fail "--render drew a ${width}x${height} frame, which is not a window"
    }
    Say "--render drew a ${width}x${height} frame"

    Write-Host "`nthe Windows release works as downloaded"
} finally {
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
