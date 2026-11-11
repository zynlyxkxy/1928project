# =====================================================================
#  sync_proto.ps1 - copy ONE protocol/data file downstream only if it
#                   actually changed.
#
#  Same intent as the old `fc /b` + `copy /Y` pair that every build.bat
#  carried:
#    - content identical  -> do NOT touch the target (mtime stays), so
#                            downstream regeneration / compilation is not
#                            needlessly retriggered
#    - content different  -> overwrite
#  Difference from the old code: a MISSING SOURCE is reported as an error
#  (old behaviour: zero output, "success"), and there is a machine-readable
#  exit code.
#
#  Usage: powershell -NoProfile -ExecutionPolicy Bypass -File sync_proto.ps1
#         -Src <file> -Dst <file> [-DryRun]
#
#  exit 0 : up to date, or copied
#  exit 1 : source missing (nothing was written)
#
#  Last stdout line is the machine-readable sentinel "[sync] rc=0" (ok) or
#  "[sync] rc=1" (failed): scripts\private_scripts\_sync_ops.bat decides success from that
#  line and treats a MISSING one as a failure (this script died before it
#  could report).
# =====================================================================
param(
    [Parameter(Mandatory=$true)][string]$Src,
    [Parameter(Mandatory=$true)][string]$Dst,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$name = Split-Path -Leaf $Src

if (-not (Test-Path -LiteralPath $Src -PathType Leaf)) {
    Write-Host ("  [MISS] source not found: " + $Src)
    Write-Host "[sync] rc=1"
    exit 1
}

if (Test-Path -LiteralPath $Dst -PathType Leaf) {
    $a = (Get-FileHash -LiteralPath $Src -Algorithm SHA256).Hash
    $b = (Get-FileHash -LiteralPath $Dst -Algorithm SHA256).Hash
    if ($a -eq $b) {
        Write-Host ("  [skip] " + $name + " [unchanged]")
        Write-Host "[sync] rc=0"
        exit 0
    }
}

if ($DryRun) {
    Write-Host ("  [diff] " + $name + " (dry-run: would copy)")
    Write-Host "[sync] rc=0"
    exit 0
}

$dir = Split-Path -Parent $Dst
if ($dir -and -not (Test-Path -LiteralPath $dir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}
Copy-Item -LiteralPath $Src -Destination $Dst -Force
Write-Host ("  [OK]   " + $name)
Write-Host "[sync] rc=0"
exit 0
