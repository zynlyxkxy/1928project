# ============================================================
#  _tag_version_main.ps1 - build the release map from tags.
#
#  Reads engineering/version/RELEASE.md AS COMMITTED (from git, not
#  the working tree) and writes:
#      engineering/version/CHANGELOG.md
#      engineering/version/release-map.html
#
#  Nothing is tagged, committed or pushed: tags are made by hand.
#  One section of RELEASE.md is one main tag; its heading carries the
#  tag name without the prefix:
#
#      ## 主仓 2.1.0                          monthly, no suffix
#      ## 主仓 2.0.1_Rel_2026110901           mid-month temporary tag
#
#  Prefix is prepended, so the tag above is <Prefix>-2.1.0 etc.  A
#  section whose tag does not exist is kept in the map and marked.
#
#  For every release the 5 gitlinks are read at the tag's commit, and
#  the sub-repo tag at each bound commit is looked up on that repo's
#  origin:
#    - tag found : label = tag; notes = the ">>>>> <label>" block of
#                  VERSION.txt (read at the newest bound commit), matched
#                  exactly
#    - no tag    : label = the commit hash, marked "未打标签"
#  Missing notes are written as "版本迭代描述缺失".
#
#  Git is read in batches: one ref listing, one cat-file --batch for
#  every gitlink, one ls-remote and one cat-file --batch per sub-repo.
#
#  NOTE: this file is UTF-8 WITH BOM on purpose: PowerShell 5.1
#  reads UTF-8 without BOM as ANSI and would corrupt the Chinese
#  headings written into the generated files.
# ============================================================
param(
    # ========== project settings (edit for a new project) ==========
    [string]$Prefix = 'ICB3000M_WM',                          # main tag prefix
    [string]$RepoRoot = '',                                    # git repo to read; '' = the repo this script lives in
    [string]$ReleaseDoc = 'engineering/version/RELEASE.md',    # human-written source
    [string]$ChangelogPath = 'engineering/version/CHANGELOG.md',
    [string]$HtmlPath = 'engineering/version/release-map.html',
    [string]$HtmlTemplate = 'engineering/version/private_tools/release-map.template.html',
    [object[]]$SubRepos = @(
        @{ Repo = 'gui';           VersionFile = 'VERSION.txt' }
        @{ Repo = 'mctrl';         VersionFile = 'VERSION.txt' }
        @{ Repo = 'module/re';     VersionFile = 'VERSION.txt' }
        @{ Repo = 'module/io';     VersionFile = 'VERSION.txt' }
        @{ Repo = 'module/motion'; VersionFile = 'VERSION.txt' }
    ),
    # ========== run options ==========
    [switch]$DryRun,
    [Alias('h')][switch]$Help
)

$ErrorActionPreference = 'Stop'

# Written where a release has no usable notes.
$MissingNotes = '版本迭代描述缺失'
$MissingTag = '标签不存在'
$NoSubTag = '未打标签'

# Per-run cache of sub-repo tags, keyed by repo path.
$script:SubSnaps = @{}

# -File silently drops named arguments that match no parameter; refuse the
# leftovers so a stale caller cannot think it ran something else.
if ($args.Count -gt 0) {
    Write-Host ("[ERROR] unexpected argument(s): " + ($args -join ' '))
    exit 1
}

# Project settings are read-only from the command line: one caller
# overriding them would split the map into two tag families.
foreach ($k in 'Prefix', 'ReleaseDoc', 'ChangelogPath', 'HtmlPath', 'HtmlTemplate', 'SubRepos') {
    if ($PSBoundParameters.ContainsKey($k)) {
        Write-Host ("[ERROR] " + $k + " is a project setting, not a command-line option")
        exit 1
    }
}

# The prefix is matched exactly as configured: git tags are case sensitive,
# so a wrong case must fail to match instead of being silently "fixed".
$Prefix = $Prefix.Trim()

# ============================================================
#  Helpers
# ============================================================
function Show-Usage {
    Write-Host ""
    Write-Host "Usage: tag_version.bat [-DryRun]"
    Write-Host ""
    Write-Host "  Reads the '## 主仓 <X.Y.Z>' / '## 主仓 <X.Y.Z>_Rel_<YYYYMMDD><NN>'"
    Write-Host "  sections of the committed $ReleaseDoc and writes:"
    Write-Host "    $ChangelogPath"
    Write-Host "    $HtmlPath"
    Write-Host ""
    Write-Host "  Tag        : <Prefix>-<the name written in the section>"
    Write-Host "  Sub-repo   : tag at the bound commit, else the commit hash"
    Write-Host "  No notes   : '$MissingNotes'"
    Write-Host "  No tag     : the release stays in the map, marked '$MissingTag'"
    Write-Host ""
    Write-Host "  -DryRun : build and report, write nothing."
    Write-Host "  Nothing is tagged, committed or pushed: tags are made by hand."
    Write-Host ""
}

function Invoke-Git {
    param([string]$Repo, [string[]]$GitArgs)
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & git -C $Repo @GitArgs 2>&1
        $code = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $old }
    $lines = @()
    foreach ($l in @($out)) { if ($null -ne $l) { $lines += [string]$l } }
    return [pscustomobject]@{ Code = $code; Lines = $lines }
}

function Get-RepoRoot {
    param([string]$StartDir)
    $r = Invoke-Git $StartDir @('rev-parse', '--show-toplevel')
    if ($r.Code -ne 0) { throw ("not inside a git repository: " + $StartDir) }
    return (($r.Lines -join '').Trim())
}

function Get-RepoTagLabel {
    # A sub-repo tag is <prefix>-<label>, where the label is whatever the repo
    # uses: 2.1.0, 2.0.1_Rel_2026110901, 2.0.1-hotfix. The label is taken
    # verbatim (everything after the last dash); no shape is required.
    param([string]$Tag)
    $i = $Tag.LastIndexOf('-')
    if ($i -ge 0 -and $i -lt ($Tag.Length - 1)) { return $Tag.Substring($i + 1) }
    return $Tag
}

function Get-LabelRank {
    param([string]$Label)
    $m = [regex]::Match($Label, '(\d+)\.(\d+)\.(\d+)')
    if (-not $m.Success) { return $null }
    return [pscustomobject]@{
        Major = [int]$m.Groups[1].Value
        Minor = [int]$m.Groups[2].Value
        Patch = [int]$m.Groups[3].Value
    }
}

function Get-TagIsBetter {
    # Only a tie-break when one commit carries several tags: prefer a label
    # that contains a version number, then the larger one, then ordinal.
    param($Candidate, $Current)
    $ra = Get-LabelRank $Candidate.Label
    $rb = Get-LabelRank $Current.Label
    if ($null -ne $ra -and $null -eq $rb) { return $true }
    if ($null -eq $ra -and $null -ne $rb) { return $false }
    if ($null -ne $ra -and $null -ne $rb) {
        $c = Compare-Version $ra $rb
        if ($c -ne 0) { return ($c -gt 0) }
    }
    return ([string]::CompareOrdinal($Candidate.Label, $Current.Label) -gt 0)
}

function Get-SubVersionFile {
    # Per-repo version file; omitted means VERSION.txt.
    param($Sub)
    if ($null -ne $Sub.VersionFile -and -not [string]::IsNullOrWhiteSpace([string]$Sub.VersionFile)) {
        return [string]$Sub.VersionFile
    }
    return 'VERSION.txt'
}

function Compare-Version {
    param($A, $B)
    if ($A.Major -ne $B.Major) { if ($A.Major -gt $B.Major) { return 1 } else { return -1 } }
    if ($A.Minor -ne $B.Minor) { if ($A.Minor -gt $B.Minor) { return 1 } else { return -1 } }
    if ($A.Patch -ne $B.Patch) { if ($A.Patch -gt $B.Patch) { return 1 } else { return -1 } }
    return 0
}

function Get-GitFileText {
    # Native stdout is decoded with the console code page by PS 5.1,
    # so read blobs through a file and decode as UTF-8 instead.
    param([string]$Repo, [string]$Spec)
    $tmp = [System.IO.Path]::GetTempFileName()
    try {
        $cmd = 'git -C "' + $Repo + '" show "' + $Spec + '" > "' + $tmp + '"'
        $old = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { & cmd /c $cmd | Out-Null; $code = $LASTEXITCODE } finally { $ErrorActionPreference = $old }
        if ($code -ne 0) { return $null }
        $text = [System.IO.File]::ReadAllText($tmp, [System.Text.Encoding]::UTF8)
        if ($text.Length -gt 0 -and [int]$text[0] -eq 0xFEFF) { $text = $text.Substring(1) }
        return $text
    }
    finally { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
}

function Write-Utf8NoBom {
    param([string]$Path, [string]$Text)
    $dir = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($dir) -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

function Invoke-BatchObjects {
    # One `git cat-file --batch` for a whole list of specs. Returns one
    # entry per spec, in request order: $null when git has no such object.
    param([string]$Repo, [string[]]$Specs)
    if ($null -eq $Specs -or $Specs.Count -eq 0) { return @() }

    $inFile = [System.IO.Path]::GetTempFileName()
    $outFile = [System.IO.Path]::GetTempFileName()
    try {
        Write-Utf8NoBom $inFile (($Specs -join "`n") + "`n")
        $cmd = 'git -C "' + $Repo + '" cat-file --batch < "' + $inFile + '" > "' + $outFile + '"'
        $old = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { & cmd /c $cmd | Out-Null; $code = $LASTEXITCODE } finally { $ErrorActionPreference = $old }
        if ($code -ne 0) { throw ("git cat-file --batch failed in " + $Repo) }

        $bytes = [System.IO.File]::ReadAllBytes($outFile)
        $result = New-Object System.Collections.ArrayList
        $pos = 0
        foreach ($spec in $Specs) {
            if ($pos -ge $bytes.Length) { [void]$result.Add($null); continue }
            $nl = $pos
            while ($nl -lt $bytes.Length -and $bytes[$nl] -ne 10) { $nl++ }
            $header = [System.Text.Encoding]::ASCII.GetString($bytes, $pos, $nl - $pos)
            $pos = $nl + 1
            $parts = $header.Split(' ')
            if ($parts.Count -lt 3) { [void]$result.Add($null); continue }
            $size = [int]$parts[2]
            if ($pos + $size -gt $bytes.Length) { [void]$result.Add($null); break }
            $text = [System.Text.Encoding]::UTF8.GetString($bytes, $pos, $size)
            $pos += $size
            if ($pos -lt $bytes.Length -and $bytes[$pos] -eq 10) { $pos++ }
            [void]$result.Add([pscustomobject]@{
                Oid = $parts[0]; Type = $parts[1]; Text = $text
            })
        }
        return @($result)
    }
    finally {
        Remove-Item -LiteralPath $inFile -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue
    }
}

function Get-TagCommitMap {
    # Every tag of one repo -> the commit it points at, in one call.
    param([string]$Repo)
    $r = Invoke-Git $Repo @('for-each-ref', '--format=%(refname:short)%09%(objectname)%09%(*objectname)', 'refs/tags')
    if ($r.Code -ne 0) { throw ("cannot list tags in " + $Repo) }
    $map = @{}
    foreach ($line in $r.Lines) {
        $p = $line -split "`t"
        if ($p.Count -lt 3) { continue }
        $commit = $p[2]
        if ([string]::IsNullOrWhiteSpace($commit)) { $commit = $p[1] }
        $map[$p[0]] = $commit.Trim()
    }
    return $map
}

function Get-Gitlinks {
    # path -> commit for the configured sub-repos at a revision. One
    # ls-tree per release: a gitlink commit is not an object of this repo,
    # so cat-file cannot read it, but the tree entry carries it.
    param([string]$Repo, [string]$Rev, [string[]]$Paths)
    $map = @{}
    $r = Invoke-Git $Repo (@('ls-tree', $Rev, '--') + $Paths)
    if ($r.Code -ne 0) { throw ("cannot read the gitlinks of " + $Rev) }
    foreach ($ln in $r.Lines) {
        $m = [regex]::Match($ln, '^160000 commit ([0-9a-f]{40})\t(.+)$')
        if ($m.Success) { $map[$m.Groups[2].Value] = $m.Groups[1].Value }
    }
    return $map
}

function Get-ReleaseSections {
    # One "## ..." heading is one release. The heading carries the tag name
    # WITHOUT the prefix, in whatever shape the project uses:
    #      ## 主仓 2.1.0
    #      ## 主仓 2.0.1_Rel_2026110901
    #      ## 主仓 2.0.1-hotfix
    # Nothing is parsed out of it: that text IS the name to look up. A name
    # may also stand alone on a body line as "标签：<name>".
    param([string]$Text)
    $lines = @($Text -split '\r?\n')
    $sections = New-Object System.Collections.ArrayList
    $cur = $null
    foreach ($line in $lines) {
        if ($line -match '^##\s') {
            if ($null -ne $cur) { [void]$sections.Add($cur) }
            $heading = ($line -replace '^##\s+', '').Trim()
            $cur = [pscustomobject]@{
                Heading = $heading
                Name    = ''
                Body    = New-Object System.Collections.ArrayList
            }
            $rest = ($heading -replace '^\s*主仓\s*', '').Trim()
            if (-not [string]::IsNullOrWhiteSpace($rest)) {
                # First whitespace-separated chunk: "2.3.1 说明" -> "2.3.1".
                # Trim again: stray leading/trailing spaces are common.
                $cur.Name = (($rest -split '\s+')[0]).Trim()
            }
            continue
        }
        if ($null -eq $cur) { continue }
        $l = [string]$line
        if ($l.StartsWith("`t")) { $l = $l.Substring(1) }
        [void]$cur.Body.Add($l)
    }
    if ($null -ne $cur) { [void]$sections.Add($cur) }

    # A name may stand alone on its own line instead of in the heading.
    foreach ($s in $sections) {
        if (-not [string]::IsNullOrWhiteSpace($s.Name)) { continue }
        for ($i = 0; $i -lt $s.Body.Count; $i++) {
            $m = [regex]::Match([string]$s.Body[$i], '^\s*(?:主仓\s*)?(?:(?:tag|标签)\s*[:：]\s*)(\S+)\s*$')
            if (-not $m.Success) { continue }
            $s.Name = ($m.Groups[1].Value).Trim()
            $s.Body.RemoveAt($i)
            break
        }
    }

    foreach ($s in $sections) {
        while ($s.Body.Count -gt 0 -and ([string]$s.Body[0]).Trim() -eq '') { $s.Body.RemoveAt(0) }
        while ($s.Body.Count -gt 0 -and ([string]$s.Body[$s.Body.Count - 1]).Trim() -eq '') { $s.Body.RemoveAt($s.Body.Count - 1) }
    }
    return @($sections)
}

function Get-SubSnapshot {
    # Tags of one sub-repo as origin reports them. The sub-repos carry no
    # tag tool, so origin is the only tag record. One ls-remote per repo.
    param([string]$MainRoot, $Sub)
    $repoPath = [string]$Sub.Repo
    if ($script:SubSnaps.ContainsKey($repoPath)) { return $script:SubSnaps[$repoPath] }

    $dir = Join-Path $MainRoot $repoPath
    if (-not (Test-Path -LiteralPath $dir)) {
        throw ("sub-repo not checked out: " + $repoPath + " (git submodule update --init " + $repoPath + ")")
    }
    $r = Invoke-Git $dir @('ls-remote', '--tags', 'origin')
    if ($r.Code -ne 0) {
        throw ("cannot read tags from " + $repoPath + " origin (network/auth?)")
    }

    # An annotated tag lists twice: the tag object, then "<name>^{}" for
    # the commit it points at. Keep the commit.
    $raw = @{}
    $peeled = @{}
    foreach ($line in $r.Lines) {
        $parts = $line -split "`t"
        if ($parts.Count -lt 2) { continue }
        $ref = $parts[1]
        if ($ref -notlike 'refs/tags/*') { continue }
        $name = $ref.Substring('refs/tags/'.Length)
        if ($name.EndsWith('^{}')) { $peeled[$name.Substring(0, $name.Length - 3)] = $parts[0] }
        else { $raw[$name] = $parts[0] }
    }

    $byCommit = @{}
    $anyAtCommit = @{}
    foreach ($name in $raw.Keys) {
        $commit = $raw[$name]
        if ($peeled.ContainsKey($name)) { $commit = $peeled[$name] }
        if (-not $anyAtCommit.ContainsKey($commit)) { $anyAtCommit[$commit] = @() }
        $anyAtCommit[$commit] += $name

        $t = [pscustomobject]@{
            Tag    = $name
            Label  = (Get-RepoTagLabel $name)
            Commit = $commit
        }
        if (-not $byCommit.ContainsKey($commit)) { $byCommit[$commit] = $t; continue }
        # Several tags on one commit is not supposed to happen; pick one
        # deterministically so the output is stable.
        if (Get-TagIsBetter $t $byCommit[$commit]) { $byCommit[$commit] = $t }
    }

    $snap = [pscustomobject]@{
        Dir = $dir; ByCommit = $byCommit; AnyAtCommit = $anyAtCommit
    }
    $script:SubSnaps[$repoPath] = $snap
    return $snap
}

function Test-BlockMatch {
    # Exact string matching between a VERSION.txt marker and a tag label:
    # only surrounding whitespace is ignored, nothing else. The marker is
    # whatever the author wrote, so both forms below are equally valid and
    # each only matches the identical label:
    #     2.0.1                    ~  2.0.1                    match
    #     2.0.1_Rel_2026110901     ~  2.0.1_Rel_2026110901     match
    #     2.0.1                    ~  2.0.1_Rel_2026110901     do not match
    param([string]$Marker, [string]$Label)
    $Marker = $Marker.Trim()
    $Label = $Label.Trim()
    if ([string]::IsNullOrWhiteSpace($Marker) -or [string]::IsNullOrWhiteSpace($Label)) { return $false }
    return ($Marker -eq $Label)
}

function Get-VersionBlockBody {
    # Body of the ">>>>> <label>" block in a VERSION.txt: the lines after
    # the marker, up to the next marker or EOF.
    param([string]$Text, [string]$Label)
    $out = @()
    $collect = $false
    foreach ($l in ($Text -split '\r?\n')) {
        if ($l -match '^>>>>>\s+(\S+)\s*$') {
            if ($collect) { break }
            if (Test-BlockMatch $Matches[1] $Label) { $collect = $true }
            continue
        }
        if ($collect) { $out += $l }
    }
    while ($out.Count -gt 0 -and $out[$out.Count - 1].Trim() -eq '') { $out = @($out[0..($out.Count - 2)]) }
    return @($out)
}

function Format-Date {
    param([string]$Date)
    if ($Date.Length -ne 8) { return $Date }
    return ($Date.Substring(0, 4) + "-" + $Date.Substring(4, 2) + "-" + $Date.Substring(6, 2))
}

function Escape-Csv {
    param([string]$Value)
    $s = [string]$Value
    if ($s -match '[",\r\n]') { return '"' + $s.Replace('"', '""') + '"' }
    return $s
}

# ============================================================
#  Main
# ============================================================
if ($Help) { Show-Usage; exit 0 }
if ([string]::IsNullOrWhiteSpace($Prefix)) { Write-Host "[ERROR] project setting Prefix is empty"; exit 1 }
foreach ($k in 'ReleaseDoc', 'ChangelogPath', 'HtmlPath') {
    if ([string]::IsNullOrWhiteSpace([string](Get-Variable -Name $k -ValueOnly))) {
        Write-Host ("[ERROR] project setting " + $k + " is empty")
        exit 1
    }
}
# Template left empty means "the one next to this script": that is how the
# package is handed out, so a new project needs no path for it.
if ([string]::IsNullOrWhiteSpace($HtmlTemplate)) {
    $HtmlTemplate = Join-Path $PSScriptRoot 'release-map.template.html'
}
foreach ($s in $SubRepos) {
    if ([string]::IsNullOrWhiteSpace([string]$s.Repo)) {
        Write-Host "[ERROR] every SubRepos entry needs Repo (the sub-repo path); VersionFile is optional"
        exit 1
    }
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $root = Get-RepoRoot $PSScriptRoot
}
else {
    if (-not (Test-Path -LiteralPath $RepoRoot)) {
        Write-Host ("[ERROR] project setting RepoRoot does not exist: " + $RepoRoot)
        exit 1
    }
    $root = Get-RepoRoot (Resolve-Path -LiteralPath $RepoRoot).Path
}
$paths = @($SubRepos | ForEach-Object { $_.Repo })

# ---- 1. the release list comes from the committed RELEASE.md ----
# The working tree is deliberately ignored: the tagged history is the
# source, and an uncommitted edit must not change the map.
$text = Get-GitFileText $root ('HEAD:' + $ReleaseDoc)
if ($null -eq $text) {
    Write-Host ("[ERROR] release doc not committed: " + $ReleaseDoc)
    exit 1
}
$sections = @(Get-ReleaseSections $text)
if ($sections.Count -eq 0) {
    Write-Host ("[ERROR] no '## 主仓 X.Y.Z' section in " + $ReleaseDoc)
    exit 1
}

# ---- 2. one ref listing: every tag -> commit ----
$mainTags = Get-TagCommitMap $root

# ---- 3. resolve the sections and their bindings ----
$releases = @()
foreach ($s in $sections) {
    $name = [string]$s.Name
    $tag = ''
    if (-not [string]::IsNullOrWhiteSpace($name)) { $tag = $Prefix + '-' + $name }

    $commit = ''
    if (-not [string]::IsNullOrWhiteSpace($tag) -and $mainTags.ContainsKey($tag)) {
        $commit = [string]$mainTags[$tag]
    }

    if ([string]::IsNullOrWhiteSpace($commit)) {
        Write-Host ("[WARN] " + $MissingTag + ": " + $(if ($tag) { $tag } else { $s.Heading }))
        $releases += [pscustomobject]@{
            Tag = $tag; Label = $name; Date = ''; Body = ($s.Body -join "`n")
            Repos = @(); Found = $false
        }
        continue
    }

    # The release date is the commit date: the tag name is not parsed.
    $relDate = ''
    $cd = Invoke-Git $root @('log', '-1', '--format=%ad', '--date=format:%Y%m%d', $commit)
    if ($cd.Code -eq 0) { $relDate = (($cd.Lines -join '').Trim()) }

    $rel = [pscustomobject]@{
        Tag = $tag; Label = $name; Date = $relDate; Body = ($s.Body -join "`n")
        Repos = @(); Found = $true; Commit = $commit
    }

    # One ls-tree per release keeps the 5 gitlinks in a single git call.
    $links = Get-Gitlinks $root $commit $paths
    $rows = @()
    foreach ($sub in $SubRepos) {
        $sha = ''
        if ($links.ContainsKey([string]$sub.Repo)) { $sha = [string]$links[[string]$sub.Repo] }
        $rows += [pscustomobject]@{ Repo = [string]$sub.Repo; Commit = $sha }
    }
    $rel.Repos = $rows
    $releases += $rel
}

# ---- 5. sub tags: one ls-remote per repo, then map commit -> tag ----
foreach ($rel in $releases) {
    if (-not $rel.Found) { continue }
    foreach ($row in $rel.Repos) {
        $sub = $null
        foreach ($x in $SubRepos) { if ([string]$x.Repo -eq $row.Repo) { $sub = $x } }
        if ([string]::IsNullOrWhiteSpace($row.Commit)) { continue }
        $snap = Get-SubSnapshot $root $sub
        if ($snap.ByCommit.ContainsKey($row.Commit)) {
            $t = $snap.ByCommit[$row.Commit]
            $row | Add-Member -NotePropertyName Tag -NotePropertyValue ([string]$t.Tag) -Force
            $row | Add-Member -NotePropertyName Label -NotePropertyValue ([string]$t.Label) -Force
            if ($snap.AnyAtCommit.ContainsKey($row.Commit) -and @($snap.AnyAtCommit[$row.Commit]).Count -gt 1) {
                Write-Host ("[WARN] " + $row.Repo + " " + $row.Commit.Substring(0, 7) +
                            " has more than one tag; using " + $t.Tag)
            }
        }
        else {
            $row | Add-Member -NotePropertyName Tag -NotePropertyValue $row.Commit.Substring(0, 7) -Force
            $row | Add-Member -NotePropertyName Label -NotePropertyValue '' -Force
        }
    }
}

# ---- 6. notes: one VERSION.txt per sub-repo ----
# VERSION.txt is the repo's version catalogue, so the copy at the newest
# binding is the final text: every release reads its block from there,
# not from whatever the repo happened to have at that old commit.
foreach ($sub in $SubRepos) {
    $dir = Join-Path $root $sub.Repo

    $authoritative = ''
    foreach ($rel in $releases) {
        if (-not $rel.Found) { continue }
        foreach ($row in $rel.Repos) {
            if ($row.Repo -ne $sub.Repo) { continue }
            if ([string]::IsNullOrWhiteSpace($row.Commit)) { continue }
            if ([string]::IsNullOrWhiteSpace($row.Label)) { continue }
            $authoritative = $row.Commit
            break
        }
        if (-not [string]::IsNullOrWhiteSpace($authoritative)) { break }
    }

    $catalogue = $null
    if (-not [string]::IsNullOrWhiteSpace($authoritative)) {
        $spec = $authoritative + ':' + (Get-SubVersionFile $sub)
        $has = Invoke-Git $dir @('cat-file', '-e', ($authoritative + '^{commit}'))
        if ($has.Code -ne 0) {
            $f = Invoke-Git $dir @('fetch', 'origin')
            if ($f.Code -ne 0) {
                throw ("cannot fetch " + $sub.Repo + ": " + ($f.Lines -join ' '))
            }
        }
        $blobs = @(Invoke-BatchObjects $dir @($spec))
        if ($blobs.Count -gt 0 -and $null -ne $blobs[0]) { $catalogue = [string]$blobs[0].Text }
    }

    foreach ($rel in $releases) {
        if (-not $rel.Found) { continue }
        foreach ($row in $rel.Repos) {
            if ($row.Repo -ne $sub.Repo) { continue }
            $row | Add-Member -NotePropertyName Notes -NotePropertyValue '' -Force
            if ($null -eq $catalogue) { continue }
            if ([string]::IsNullOrWhiteSpace([string]$row.Label)) { continue }
            $body = ((Get-VersionBlockBody $catalogue ([string]$row.Label)) -join "`n").TrimEnd()
            if (-not [string]::IsNullOrWhiteSpace($body)) { $row.Notes = $body }
        }
    }

}

if ($releases.Count -eq 0) {
    Write-Host ("[ERROR] no release could be read: check the tag names in " + $ReleaseDoc)
    exit 1
}

# ============================================================
#  Reports
# ============================================================
function Write-Reports {
    param([string]$Root, [array]$Model, [array]$SubRepos)

    # ---- rows for the html table: one row per repo per release ----
    # Kept in memory only; the html template embeds them. Oldest release
    # last, so the first row of a tag is its "previous" comparison base.
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine('main_tag,main_version,main_date,repo,repo_tag,repo_version,prev_repo_tag,changed,status')
    for ($i = $Model.Count - 1; $i -ge 0; $i--) {
        $rel = $Model[$i]
        $prev = $null
        if ($i -lt ($Model.Count - 1)) { $prev = $Model[$i + 1] }
        foreach ($s in $SubRepos) {
            $r = $null
            foreach ($x in $rel.Repos) { if ($x.Repo -eq $s.Repo) { $r = $x } }
            if ($null -eq $r) {
                # A release whose tag is missing still gets one row per repo,
                # so the UI can show it instead of silently dropping it.
                $r = [pscustomobject]@{ Repo = $s.Repo; Commit = ''; Tag = ''; Version = ''; Notes = '' }
            }

            $prevTag = ''
            if ($null -ne $prev) {
                foreach ($x in $prev.Repos) { if ($x.Repo -eq $r.Repo) { $prevTag = [string]$x.Tag } }
            }
            $changed = '1'
            if ($null -ne $prev -and $prevTag -eq ([string]$r.Tag) -and -not [string]::IsNullOrWhiteSpace($prevTag)) {
                $changed = '0'
            }
            $status = 'ok'
            if (-not $rel.Found) { $status = 'no_main_tag' }
            elseif ([string]::IsNullOrWhiteSpace([string]$r.Commit)) { $status = 'no_binding' }
            elseif ([string]::IsNullOrWhiteSpace([string]$r.Label)) { $status = 'no_sub_tag' }
            elseif ([string]::IsNullOrWhiteSpace([string]$r.Notes)) { $status = 'no_notes' }

            $row = @(
                (Escape-Csv $rel.Tag), (Escape-Csv $rel.Label), (Escape-Csv (Format-Date $rel.Date)),
                (Escape-Csv $r.Repo), (Escape-Csv ([string]$r.Tag)), (Escape-Csv ([string]$r.Label)),
                (Escape-Csv $prevTag), $changed, $status
            )
            [void]$sb.AppendLine(($row -join ','))
        }
    }

    # ---- CHANGELOG: newest first, one section per release ----
    $md = New-Object System.Text.StringBuilder
    [void]$md.AppendLine('# ' + $Prefix + ' 主仓版本记录')
    [void]$md.AppendLine('')
    [void]$md.AppendLine('> 本文件由脚本生成，请勿手工修改。最新版本在最前。')
    [void]$md.AppendLine('')
    for ($i = 0; $i -lt $Model.Count; $i++) {
        $rel = $Model[$i]
        $prev = $null
        if ($i -lt ($Model.Count - 1)) { $prev = $Model[$i + 1] }

        # The heading carries the tag name without the prefix, so one tag
        # (and not just one version) owns one section.
        $relLabel = [string]$rel.Label
        if ([string]::IsNullOrWhiteSpace($relLabel) -and
            -not [string]::IsNullOrWhiteSpace([string]$rel.Tag) -and
            ([string]$rel.Tag).StartsWith($Prefix + '-')) {
            $relLabel = ([string]$rel.Tag).Substring($Prefix.Length + 1)
        }
        [void]$md.AppendLine('## 主仓 ' + $relLabel)
        [void]$md.AppendLine('')
        [void]$md.AppendLine('发布日期：' + $(if ($rel.Found) { Format-Date $rel.Date } else { '—' }))
        [void]$md.AppendLine('')
        [void]$md.AppendLine('| 主仓 tag | ' + (($SubRepos | ForEach-Object { $_.Repo }) -join ' | ') + ' |')
        [void]$md.AppendLine('|---|' + (($SubRepos | ForEach-Object { '---' }) -join '|') + '|')
        $cells = @()
        foreach ($s in $SubRepos) {
            $t = '-'
            foreach ($r in $rel.Repos) {
                if ($r.Repo -eq $s.Repo -and -not [string]::IsNullOrWhiteSpace([string]$r.Tag)) { $t = [string]$r.Tag }
            }
            $cells += $t
        }
        $mainCell = $rel.Tag
        if (-not $rel.Found) { $mainCell = $(if ($rel.Tag) { $rel.Tag } else { '—' }) + '（' + $MissingTag + '）' }
        [void]$md.AppendLine('| ' + $mainCell + ' | ' + ($cells -join ' | ') + ' |')
        [void]$md.AppendLine('')

        # 主仓本版更新内容
        if (-not [string]::IsNullOrWhiteSpace($rel.Body)) {
            [void]$md.AppendLine($rel.Body)
            [void]$md.AppendLine('')
        }

        if (-not $rel.Found) {
            [void]$md.AppendLine('---')
            [void]$md.AppendLine('')
            continue
        }

        foreach ($s in $SubRepos) {
            $cur = $null
            foreach ($r in $rel.Repos) { if ($r.Repo -eq $s.Repo) { $cur = $r } }
            if ($null -eq $cur -or [string]::IsNullOrWhiteSpace([string]$cur.Tag)) { continue }
            $old = $null
            if ($null -ne $prev) {
                foreach ($r in $prev.Repos) { if ($r.Repo -eq $s.Repo) { $old = $r } }
            }
            $curLabel = [string]$cur.Tag
            $oldLabel = ''
            if ($null -ne $old) { $oldLabel = [string]$old.Tag }
            if ($oldLabel -eq $curLabel) { continue }

            $newText = $(if ($cur.Label) { [string]$cur.Label } else { [string]$cur.Tag + '（' + $NoSubTag + '）' })
            $title = '### 子仓 ' + $s.Repo + '：' + $newText
            if ($null -ne $old -and -not [string]::IsNullOrWhiteSpace($oldLabel)) {
                $oldText = $(if ($old.Label) { [string]$old.Label } else { [string]$old.Tag + '（' + $NoSubTag + '）' })
                $title = '### 子仓 ' + $s.Repo + '：' + $oldText + ' → ' + $newText
            }
            [void]$md.AppendLine($title)
            [void]$md.AppendLine('')
            if ([string]::IsNullOrWhiteSpace([string]$cur.Label)) {
                [void]$md.AppendLine($MissingNotes + '（该提交没有子仓标签）')
            }
            elseif ([string]::IsNullOrWhiteSpace([string]$cur.Notes)) {
                [void]$md.AppendLine($MissingNotes + '（' + (Get-SubVersionFile $s) + ' 里没有 ' + $cur.Label + ' 的描述块）')
            }
            else {
                [void]$md.AppendLine([string]$cur.Notes)
            }
            [void]$md.AppendLine('')
        }
        [void]$md.AppendLine('---')
        [void]$md.AppendLine('')
    }

    if ($DryRun) {
        Write-Host "[OK] dry run: nothing written"
        return
    }

    Write-Utf8NoBom (Join-Path $Root $ChangelogPath) $md.ToString()

    # ---- HTML: inject the generated markdown and the table rows ----
    # The rows stay in memory: there is no CSV file on disk anymore.
    $tplPath = $HtmlTemplate
    if (-not [System.IO.Path]::IsPathRooted($tplPath)) { $tplPath = Join-Path $Root $tplPath }
    if (-not (Test-Path -LiteralPath $tplPath)) {
        # The package handed to another project keeps the template next to
        # the script, so a straight copy of this file works there too.
        $nearScript = Join-Path $PSScriptRoot 'release-map.template.html'
        if (Test-Path -LiteralPath $nearScript) { $tplPath = $nearScript }
    }
    if (-not (Test-Path -LiteralPath $tplPath)) {
        throw ("html template not found: " + $tplPath)
    }
    $tpl = [System.IO.File]::ReadAllText($tplPath, [System.Text.Encoding]::UTF8)
    $mdJson = ConvertTo-Json $md.ToString() -Compress
    $csvJson = ConvertTo-Json $sb.ToString() -Compress
    $mdJson = $mdJson.Replace('</', '<\/')
    $csvJson = $csvJson.Replace('</', '<\/')
    $html = $tpl.Replace('__TITLE__', ($Prefix + ' 版本更新图')).Replace('__MD__', $mdJson).Replace('__CSV__', $csvJson)
    Write-Utf8NoBom (Join-Path $Root $HtmlPath) $html

    Write-Host ("[OK] wrote " + $ChangelogPath)
    Write-Host ("[OK] wrote " + $HtmlPath)
}

$missingTagCount = @($releases | Where-Object { -not $_.Found }).Count
$noTagCount = 0
$noNotesCount = 0
foreach ($rel in $releases) {
    foreach ($r in $rel.Repos) {
        if ([string]::IsNullOrWhiteSpace([string]$r.Commit)) { continue }
        if ([string]::IsNullOrWhiteSpace([string]$r.Label)) { $noTagCount++ }
        elseif ([string]::IsNullOrWhiteSpace([string]$r.Notes)) { $noNotesCount++ }
    }
}

Write-Host ("[INFO] releases  : " + $releases.Count + " (from " + $ReleaseDoc + " at HEAD)")
if ($missingTagCount -gt 0) { Write-Host ("[WARN] no tag    : " + $missingTagCount + " release(s) marked " + $MissingTag) }
if ($noTagCount -gt 0) { Write-Host ("[WARN] sub no tag: " + $noTagCount + " binding(s) shown by commit hash") }
if ($noNotesCount -gt 0) { Write-Host ("[WARN] no notes  : " + $noNotesCount + " binding(s) marked " + $MissingNotes) }

try {
    Write-Reports $root $releases $SubRepos
    exit 0
}
catch {
    Write-Host ("[ERROR] " + $_.Exception.Message)
    exit 1
}
