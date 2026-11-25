# Locate the build tools the Windows app needs and generate uiribbon.lib.
#
# uiribbon.lib is not shipped with modern Windows SDKs, so it is built from
# the system DLL with dumpbin and lib.exe on every run. Both workflows used
# to carry their own copy of that dance, which had already drifted: the
# build.yml copy was hardcoded to x64 while the release matrix passed an
# architecture through. One script means one machine type rule and one
# place to fix when a runner image moves.
#
# Writes uicc_path, propsys_lib and uiribbon_lib to $GITHUB_OUTPUT when it
# is set, so a workflow step can consume them as step outputs.
#
# Usage:
#   ./tools/ci/find-sdk.ps1 -Arch x64
#   ./tools/ci/find-sdk.ps1 -Arch arm64 -WorkDir $env:RUNNER_TEMP/uiribbon_lib

param(
    # The link target, not the tool's own architecture: an arm64 cross build
    # has to link an arm64 import library.
    [Parameter(Mandatory = $true)][ValidateSet('x64', 'arm64')][string]$Arch,
    # Where the generated import library goes.
    [string]$WorkDir,
    # Optional roots, for a machine whose layout differs from the runner's.
    [string[]]$KitsRoots = @('C:\Program Files (x86)\Windows Kits\10',
                             'C:\Program Files\Windows Kits\10'),
    [string]$VsRoot = 'C:\Program Files\Microsoft Visual Studio\2022'
)

$ErrorActionPreference = 'Stop'

function Fail([string]$Message) {
    Write-Error $Message
    exit 1
}

function Emit([string]$Name, [string]$Value) {
    Write-Host "$Name=$Value"
    if ($env:GITHUB_OUTPUT) {
        "$Name=$Value" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    }
}

if (-not $WorkDir) { $WorkDir = Join-Path $env:RUNNER_TEMP 'uiribbon_lib' }

# The newest SDK, compared as a version: sorting the directory names as
# strings puts 10.0.9 after 10.0.26100.
$sdkBin = $null
$sdkVersion = $null
foreach ($root in $KitsRoots) {
    if (-not (Test-Path $root)) { continue }
    $candidates = Get-ChildItem (Join-Path $root 'bin') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+' }
    foreach ($candidate in $candidates) {
        $parsed = $null
        try { $parsed = [version]$candidate.Name } catch { continue }
        if (-not $sdkVersion -or $parsed -gt $sdkVersion) {
            $sdkVersion = $parsed
            $sdkBin = $candidate.FullName
        }
    }
}
if (-not $sdkBin) { Fail 'no Windows Kits 10 SDK found' }
Write-Host "SDK bin: $sdkBin"
$kitsRoot = Split-Path (Split-Path $sdkBin)

# UICC compiles the ribbon markup for any architecture, so the x64 tool is
# preferred and the x86 one is the fallback.
$uicc = $null
foreach ($dir in @('x64', 'x86', 'arm64')) {
    $path = Join-Path $sdkBin "$dir\uicc.exe"
    if (Test-Path $path) { $uicc = $path; break }
}
if (-not $uicc) {
    $found = Get-ChildItem $sdkBin -Recurse -Filter 'uicc.exe' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($found) { $uicc = $found.FullName }
}
if (-not $uicc) { Fail 'uicc.exe not found in the Windows SDK' }

# propsys.lib has to match the link target.
$propsys = Get-ChildItem (Join-Path $kitsRoot 'Lib') -Recurse -Filter 'propsys.lib' `
    -ErrorAction SilentlyContinue |
    Where-Object { $_.DirectoryName -match "\\$Arch\\" } |
    Sort-Object FullName | Select-Object -First 1
if (-not $propsys) { Fail "propsys.lib for $Arch not found in the SDK" }

# dumpbin and lib.exe come from the newest MSVC, host x64: the tools run on
# the runner whatever the target is.
$msvc = $null
if (Test-Path $VsRoot) {
    $edition = Get-ChildItem $VsRoot -Directory -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($edition) {
        $tools = Join-Path $edition.FullName 'VC\Tools\MSVC'
        if (Test-Path $tools) {
            $msvc = Get-ChildItem $tools -Directory |
                Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
                Select-Object -First 1
        }
    }
}
if (-not $msvc) { Fail 'no MSVC toolset found under the Visual Studio root' }
$dumpbin = Join-Path $msvc.FullName 'bin\Hostx64\x64\dumpbin.exe'
$libexe = Join-Path $msvc.FullName 'bin\Hostx64\x64\lib.exe'
foreach ($tool in @($dumpbin, $libexe)) {
    if (-not (Test-Path $tool)) { Fail "not found: $tool" }
}

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$defFile = Join-Path $WorkDir 'uiribbon.def'
$libFile = Join-Path $WorkDir 'uiribbon.lib'

$exports = & $dumpbin /exports "$env:SystemRoot\System32\uiribbon.dll"
if ($LASTEXITCODE -ne 0) { Fail 'dumpbin could not read uiribbon.dll exports' }
$def = "LIBRARY uiribbon.dll`nEXPORTS`n"
foreach ($line in $exports) {
    if ($line -match '^\s+\d+\s+[0-9A-F]+\s+(\S+)') {
        $def += "  $($matches[1])`n"
    }
}
if ($def -notmatch 'EXPORTS\s*\n\s+\S') {
    Fail 'the generated .def names no exports'
}
Set-Content -Path $defFile -Value $def -Encoding ASCII

& $libexe "/def:$defFile" "/out:$libFile" "/machine:$Arch"
if ($LASTEXITCODE -ne 0) { Fail 'lib.exe could not build the import library' }
if (-not (Test-Path $libFile)) { Fail "the import library was not produced: $libFile" }

Emit 'uicc_path' $uicc
Emit 'propsys_lib' $propsys.FullName
Emit 'uiribbon_lib' $libFile
Write-Host "uiribbon.lib built for $Arch"
