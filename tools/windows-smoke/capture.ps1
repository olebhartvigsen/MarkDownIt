# The Windows half of the capture pipeline: launch the app with the QA
# state walk, photograph the screen on every state announcement, and
# record each state's document rectangle next to the capture. The mac
# smoke (tools/macos-smoke/launch_smoke.sh) does the same thing with
# screencapture, and tools/compare_platform_captures.py compares the
# two sets.
#
# Usage:
#   powershell -File tools/windows-smoke/capture.ps1 `
#       -Exe build/Release/MarkDownIt.exe `
#       -Doc tests/fixtures/layout-showcase.md
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Doc,
    [string]$OutDir = "build-win/qa",
    [int]$TimeoutSeconds = 300
)

# Screen capture policy: screenshots are allowed in the build pipeline
# (GitHub Actions) and never on a developer machine. The script stops
# here anywhere else, before it touches the screen. The same rule is
# enforced by tools/check_capture_policy.py.
function Test-CiCapturePolicy {
    return ($env:GITHUB_ACTIONS -eq "true" -and $env:RUNNER_OS)
}
if (-not (Test-CiCapturePolicy)) {
    Write-Host "capture policy: screenshots are allowed only in the build pipeline (GITHUB_ACTIONS=true). Nothing was captured."
    exit 3
}

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$log = (Resolve-Path $OutDir).Path + "\walk.log"

# The app walks the states the comparison covers on both platforms.
$states = @("doc", "find", "edit", "outline", "source", "lower", "end", "tail", "tail2")
$env:MARKDOWNIT_QA_STATES = ($states -join ",")
$env:MARKDOWNIT_QA_LOG = $log

# The app opens the document it was given on the command line.
$exePath = (Resolve-Path $Exe).Path
$docPath = (Resolve-Path $Doc).Path
$proc = Start-Process -FilePath $exePath -ArgumentList "`"$docPath`"" -PassThru

function Capture-Screen([string]$path) {
    # Second check at the point of use, in case the code above is moved.
    if (-not (Test-CiCapturePolicy)) { throw "capture policy: not in the build pipeline" }
    $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty,
                      $bounds.Size)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
}

$seen = @{}
$rects = @()
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline) {
    if (Test-Path $log) {
        $lines = Get-Content $log
        foreach ($state in $states) {
            if ($seen.ContainsKey($state)) { continue }
            $hit = $lines |
                Where-Object { $_ -match "^qa state ready: $state " } |
                Select-Object -First 1
            if ($hit) {
                # One beat for the state to settle on the screen, then
                # the picture.
                Start-Sleep -Milliseconds 600
                Capture-Screen (Join-Path $OutDir "$state.png")
                $rect = ($hit -split " rect=")[1]
                $rects += "$state $rect"
                $seen[$state] = $true
                Write-Host "windows smoke: captured $state.png ($rect)"
            }
        }
        if ($lines -match "^qa states done$") { break }
    }
    if ($proc.HasExited) { break }
    Start-Sleep -Milliseconds 500
}

Set-Content -Path (Join-Path $OutDir "rects.txt") -Value $rects

# Keep the app's own trace with the captures: it names every state and
# carries the startup argument and any diagnostic, so the artifact
# explains what the run did even when it succeeded.
$appTrace = Join-Path $env:LOCALAPPDATA "MarkDownIt\crash_trace.log"
if (Test-Path $appTrace) {
    Copy-Item $appTrace (Join-Path $OutDir "crash_trace.log") -ErrorAction SilentlyContinue
}

# The walk quits the app itself when it is done; close it either way so
# the runner never hangs on a stray window.
if (-not $proc.HasExited) {
    if (-not $proc.WaitForExit(8000)) { $proc.Kill() }
}

$trace = Join-Path $env:LOCALAPPDATA "MarkDownIt\crash_trace.log"
$finished = $false
if (Test-Path $log) {
    $finished = [bool](Get-Content $log | Where-Object {
        $_ -match "^qa states done$"
    })
}
$missing = @($states | Where-Object { -not $seen.ContainsKey($_) })
$exitText = if ($proc.HasExited) { $proc.ExitCode } else { "still running" }

if ($missing.Count -gt 0 -or -not $finished) {
    Write-Host "windows smoke FAILED: missing captures: $($missing -join ', '); walk finished: $finished; exit: $exitText"
    if (Test-Path $log) { Get-Content $log -Tail 30 }
    if (Test-Path $trace) {
        Write-Host "crash trace tail:"
        Get-Content $trace -Tail 40
    }
    exit 1
}

Write-Host "windows smoke: captured $($seen.Count) states (exit $exitText)"
exit 0
