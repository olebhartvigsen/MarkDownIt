# Stage, index and pack one architecture's MSIX.
#
# Both pipelines need the same package: build.yml to prove the layout and
# the manifest still pack on every push, release.yml to ship the result.
# Before this script the steps lived inline in release.yml only, so a
# change to the manifest handling was exercised for the first time on the
# tag. Keeping one implementation is the point of the file; the two
# callers differ only in what they do with the produced package
# (references/release-pipeline.md).
#
# Usage:
#   ./tools/ci/pack-msix.ps1 -Arch x64 -Version 1.2.3.0 `
#       -Exe build/Release/MarkDownIt.exe -StageDir $env:RUNNER_TEMP/msix `
#       -OutFile $env:RUNNER_TEMP/msix/MarkDownIt-x64.msix
#
# -Version is the four-part package version (X.Y.Z.0). The caller builds
# it from the tag; this script only writes it into the identity.

param(
    [Parameter(Mandatory = $true)][string]$Arch,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$StageDir,
    [Parameter(Mandatory = $true)][string]$OutFile,
    # The exe to package. Optional: the release job downloads it into the
    # staging directory itself, so there it is already in place.
    [string]$Exe,
    # Optional: the directory holding makeappx.exe and makepri.exe. Found
    # under the Windows Kits install when it is not given.
    [string]$SdkBin,
    [string]$RepoRoot = (Get-Location).Path
)

$ErrorActionPreference = 'Stop'

function Fail([string]$Message) {
    Write-Error $Message
    exit 1
}

# The version is four-part, always: the Store rejects a three-part one,
# and a fifth part is not a version at all.
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') {
    Fail "MSIX version '$Version' is not four-part (X.Y.Z.0)"
}

if ($Exe -and -not (Test-Path $Exe)) { Fail "exe not found: $Exe" }

# The newest SDK directory, compared as a version and not as a string:
# "10.0.9" sorts after "10.0.26100" alphabetically.
if (-not $SdkBin) {
    $root = 'C:\Program Files (x86)\Windows Kits\10\bin'
    if (-not (Test-Path $root)) { Fail "Windows Kits bin not found: $root" }
    $newest = Get-ChildItem $root -Directory |
        Where-Object { $_.Name -like '10.0.*' } |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1
    if (-not $newest) { Fail "no SDK version directory under $root" }
    foreach ($candidate in @("$($newest.FullName)\x64", "$($newest.FullName)\x86")) {
        if (Test-Path (Join-Path $candidate 'makeappx.exe')) {
            $SdkBin = $candidate
            break
        }
    }
}
$makeappx = Join-Path $SdkBin 'makeappx.exe'
$makepri = Join-Path $SdkBin 'makepri.exe'
if (-not (Test-Path $makeappx)) { Fail "makeappx.exe not found under $SdkBin" }
if (-not (Test-Path $makepri)) { Fail "makepri.exe not found under $SdkBin" }

New-Item -ItemType Directory -Force -Path $StageDir | Out-Null

# The payload: the exe the build produced, and the icon set the store
# listing was made from.
if ($Exe) {
    Copy-Item $Exe (Join-Path $StageDir 'MarkDownIt.exe') -Force
}
if (-not (Test-Path (Join-Path $StageDir 'MarkDownIt.exe'))) {
    Fail "no MarkDownIt.exe in $StageDir"
}
$assetsSource = Join-Path $RepoRoot 'installer/msix/assets'
$assetsTarget = Join-Path $StageDir 'assets'
New-Item -ItemType Directory -Force -Path $assetsTarget | Out-Null
Copy-Item (Join-Path $assetsSource '*.png') $assetsTarget -Force

# The manifest: the identity carries both the version and the
# architecture. Each architecture gets its own package, because two
# packages with one full name are rejected by Partner Center. The
# Version attribute is written through a regex over the whole document,
# since the Identity element spans several lines and a line-based edit
# never reaches it; the lookbehind keeps TargetDeviceFamily MinVersion
# untouched.
$manifest = Join-Path $StageDir 'AppxManifest.xml'
$text = Get-Content (Join-Path $RepoRoot 'installer/msix/AppxManifest.xml') -Raw
$text = [regex]::Replace(
    $text,
    '(<Identity\b[^>]*?)(?<!Min)Version="[^"]*"',
    ('${1}Version="' + $Version + '"'),
    [System.Text.RegularExpressions.RegexOptions]::Singleline)
if ($text -notmatch [regex]::Escape('Version="' + $Version + '"')) {
    Fail 'the version did not reach the Identity element'
}
$identityRegex = [regex]::new('<Identity\s')
$text = $identityRegex.Replace(
    $text, ('<Identity ProcessorArchitecture="' + $Arch + '" '), 1)
Set-Content -Path $manifest -Value $text -NoNewline -Encoding UTF8

# resources.pri: without it MRT never indexes the qualified asset names
# (targetsize-*, altform-unplated), so the taskbar falls back to the
# plated path and paints BackgroundColor behind the icon. The config file
# lives outside the staging directory, which is what gets packaged.
$priConfig = Join-Path ([System.IO.Path]::GetTempPath()) "markdownit-priconfig-$Arch.xml"
& $makepri createconfig /cf $priConfig /dq en-US /o
if ($LASTEXITCODE -ne 0) { Fail 'makepri createconfig failed' }
& $makepri new /pr $StageDir /cf $priConfig /of (Join-Path $StageDir 'resources.pri') /o
if ($LASTEXITCODE -ne 0) { Fail 'makepri new failed' }
if (-not (Test-Path (Join-Path $StageDir 'resources.pri'))) {
    Fail "resources.pri missing for $Arch"
}

$outDir = Split-Path -Parent $OutFile
if ($outDir) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
& $makeappx pack -d $StageDir -p $OutFile -o
if ($LASTEXITCODE -ne 0) { Fail 'makeappx pack failed' }
if (-not (Test-Path $OutFile)) { Fail "package was not produced: $OutFile" }

$size = (Get-Item $OutFile).Length
Write-Host "packed $OutFile ($Arch, $Version, $size bytes)"
