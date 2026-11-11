# =====================================================================
#  sync_dir.ps1 - mirror-sync a directory tree with CONTENT-level diff
#
#   Makes Dst an exact copy of Src:
#     - files whose content is unchanged are NOT copied (Dst mtime stays
#       untouched, so downstream build systems do NOT treat them as modified
#       and do NOT recompile them)
#     - files whose content changed are overwritten (only their mtime moves,
#       only their dependents recompile)
#     - files present in Dst but absent in Src are DELETED (renames /
#       deletions / restructures converge to a mirror)
#
#   Decision strategy (fast):
#     Tier1: relative path + size + LastWriteTime match  -> skip (no content read)
#     Tier2: on mismatch, Get-FileHash verifies content; same content still
#            skips (protects against "Src mtime refreshed by a rebuild while
#            content is identical")
#
#   Copy order: update/add first, delete stale last (an interrupted run
#   leaves extra stale files, never missing files).
#
#   This is the single shared implementation (the per-project copies that
#   mctrl and module/re used to carry were byte-identical); it adds -DryRun
#   for `sync_ops.bat check`.
#
#   Usage: powershell -NoProfile -ExecutionPolicy Bypass -File sync_dir.ps1
#          -Src <source dir> -Dst <target dir> [-DryRun]
#
#   Last stdout line is the machine-readable sentinel "[sync] rc=0" (ok) or
#   "[sync] rc=1" (failed): scripts\private_scripts\_sync_ops.bat decides success from that
#   line and treats a MISSING one as a failure (this script died before it
#   could report).
# =====================================================================
param(
    [Parameter(Mandatory=$true)][string]$Src,
    [Parameter(Mandatory=$true)][string]$Dst,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Src)) {
    # Not reachable in practice (scripts\private_scripts\_sync_ops.bat checks the source
    # first); rc=0 mirrors this script's historical "exit 0" on this path.
    Write-Host "[sync] rc=0"
    exit 0
}

# Normalize to absolute paths (Substring(Length) needs absolute path to
# derive the relative sub-path).
$Src = (Resolve-Path -LiteralPath $Src).Path
if (Test-Path -LiteralPath $Dst) { $Dst = (Resolve-Path -LiteralPath $Dst).Path }

# Target absent: full copy (first-time mirror).
if (-not (Test-Path -LiteralPath $Dst)) {
    if ($DryRun) {
        Write-Host 'sync_dir: target absent (dry-run: would full-copy)'
        Write-Host "[sync] rc=0"
        exit 0
    }
    New-Item -ItemType Directory -Force -Path $Dst | Out-Null
    Copy-Item -Path (Join-Path $Src '*') -Destination $Dst -Recurse -Force
    Write-Host 'sync_dir: full copy (dst absent)'
    Write-Host "[sync] rc=0"
    exit 0
}

# Set of source relative paths (for stale-file removal; Windows paths are
# case-insensitive, PowerShell hashtable keys match that by default).
$srcRel = @{}
$srcFiles = @(Get-ChildItem -LiteralPath $Src -Recurse -File)
foreach ($f in $srcFiles) {
    $rel = $f.FullName.Substring($Src.Length).TrimStart('\','/')
    $srcRel[$rel] = $true
}

# 1) Add / update (Tier1 fast path, Tier2 hash confirmation).
$copied = 0; $skipped = 0
foreach ($f in $srcFiles) {
    $rel = $f.FullName.Substring($Src.Length).TrimStart('\','/')
    $t = Join-Path $Dst $rel
    if (Test-Path -LiteralPath $t -PathType Leaf) {
        $tf = Get-Item -LiteralPath $t
        if ($tf.Length -eq $f.Length -and $tf.LastWriteTimeUtc -eq $f.LastWriteTimeUtc) {
            $skipped++                       # Tier1: size + mtime equal, treat as unchanged
            continue
        }
        if ((Get-FileHash -LiteralPath $f.FullName).Hash -eq (Get-FileHash -LiteralPath $t).Hash) {
            $skipped++                       # Tier2: mtime moved but content identical, keep Dst mtime
            Write-Host ('  [skip] ' + $rel + ' [content unchanged]')
            continue
        }
    }
    if ($DryRun) {
        Write-Host ('  [diff] ' + $rel + ' (dry-run: would copy)')
        $copied++
        continue
    }
    $dir = Split-Path -Parent $t
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    Copy-Item -LiteralPath $f.FullName -Destination $t -Force
    $copied++
    Write-Host ('  [OK]   ' + $rel)
}

# 2) Delete stale files (present in Dst, absent in Src).
$removed = 0
foreach ($f in @(Get-ChildItem -LiteralPath $Dst -Recurse -File)) {
    $rel = $f.FullName.Substring($Dst.Length).TrimStart('\','/')
    if (-not $srcRel.ContainsKey($rel)) {
        if ($DryRun) {
            Write-Host ('  [stale] ' + $rel + ' (dry-run: would delete)')
        } else {
            Remove-Item -LiteralPath $f.FullName -Force
            Write-Host ('  [DEL]  ' + $rel)
        }
        $removed++
    }
}

# 3) Remove empty directories (bottom-up).
$dirs = @(Get-ChildItem -LiteralPath $Dst -Recurse -Directory |
             Sort-Object { $_.FullName.Length } -Descending)
foreach ($d in $dirs) {
    if ($null -eq (Get-ChildItem -LiteralPath $d.FullName -Force | Select-Object -First 1)) {
        if (-not $DryRun) { Remove-Item -LiteralPath $d.FullName -Force }
    }
}

if ($DryRun) {
    Write-Host ("sync_dir: dry-run, would copy=$copied, would delete=$removed, unchanged=$skipped")
} else {
    Write-Host ("sync_dir: copied=$copied skipped=$skipped removed=$removed")
}
Write-Host "[sync] rc=0"
exit 0
