@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  update_platform.bat - align the PLATFORM layer to a version.
rem
rem    update_platform.bat            align to the highest tag on
rem                                   origin/master (= "latest")
rem    update_platform.bat 2.0.1      align to tag 2.0.1
rem    update_platform.bat latest     same as no argument
rem    update_platform.bat show       show the current version of every
rem                                   platform mount, change nothing
rem    update_platform.bat -h         this help
rem
rem  Ownership, "who owns it, maintains it":
rem    this level : platform_output\basiclibrary_output, platform_output\
rem                 rpc_output, platform_output\exe_output - the ONLY
rem                 mounts this script runs git on.
rem    delegated  : module\io\basiclibrary_output, module\motion\...,
rem                 mctrl\{basiclibrary_output,platform_output},
rem                 gui\{basiclibrary_output,platform_output} and
rem                 module\re\platform - those belong to their project,
rem                 so they are NOT touched here. Each project's own
rem                 script is called with the same argument instead.
rem
rem  Atomicity is per level: every project script is all-or-nothing for
rem  the mounts it owns, and so is this level for its three. A failing
rem  level makes the whole run exit non-zero, and every failing level is
rem  named in the RESULT block. There is deliberately NO cross-level
rem  pre-check any more - the top level must not inspect mounts it does
rem  not own. A partial failure is repaired by re-running.
rem
rem  After a rollback/switch the re repo may show a dirty platform pointer:
rem  commit it there when you want to bake the version.
rem
rem  Reporting: the project scripts below are called with _CHILD_QUIET=1, so
rem  they print no report of their own - no banner, no hint block, no RESULT
rem  block, no "Produced" list. This level is the ONE place that presents the
rem  summary, so "show" ends with a single RESULT block instead of one per
rem  project. Running a project script directly (no _CHILD_QUIET) still
rem  prints its full report.
rem
rem  ASCII-only on purpose.
rem ============================================================
set "HERE=%~dp0"
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
rem  Hold policy: _SCRIPT_NO_HOLD=1 tells the scripts CALLED below not to
rem  stop; our own epilogue must still stop, so the caller's value is saved
rem  here and put back before we print the RESULT block (see :epilogue and
rem  _common.bat :hold).
set "_HOLD_IN=%_SCRIPT_NO_HOLD%"
set "_SCRIPT_NO_HOLD=1"
rem  Quiet mode for the children: they report only their per-mount lines and
rem  their [ERROR] lines; the summary below belongs to this level.
set "_CHILD_QUIET=1"

set "MOWN=platform_output\basiclibrary_output platform_output\rpc_output platform_output\exe_output"
set "OUTMOUNTS=platform_output\basiclibrary_output platform_output\rpc_output platform_output\exe_output"
set "PROJS=module\io module\motion module\re mctrl gui"
set "FAILED_LIST="

set "VER=%~1"
if /i "%VER%"=="-h" goto :usage
if /i "%VER%"=="/?" goto :usage
if /i "%VER%"=="show" goto :show
if "%VER%"=="" set "VER=latest"

where git >nul 2>nul
if errorlevel 1 (
    echo [ERROR] no git found
    set "RC=1"
    goto :epilogue
)

rem  ---- empty-workspace guard: nothing works before the submodules exist ----
if not exist "%ROOT%\platform_output\basiclibrary_output\.git" (
    echo [ERROR] platform submodules are not initialized - this is an empty workspace.
    echo         Run:  engineering\scripts\init_submodules.bat    ^(or the full chain: init_update_build.bat^)
    set "RC=1"
    goto :epilogue
)

echo.
echo ---- fetching tags in the top-level mounts ----
for %%M in (%OUTMOUNTS%) do (
    git -C "%ROOT%\%%M" fetch --tags origin master >nul 2>nul
    if errorlevel 1 echo [WARN] fetch failed in %%M - using local refs
)

if /i not "%VER%"=="latest" goto :check_tag
set "VER="
for /f %%t in ('git -C "%ROOT%\platform_output\basiclibrary_output" tag --sort=-v:refname --merged origin/master 2^>nul') do (
    if not defined VER set "VER=%%t"
)
if not defined VER (
    echo [ERROR] no tag found on origin/master of basiclibrary_output
    set "RC=1"
    goto :epilogue
)
echo [info] latest tag on master = %VER%

:check_tag
rem  All-or-nothing at this level: verify the tag in every own mount BEFORE
rem  touching any of them, so a rollback can never leave this level half done.
set "MISSING=0"
for %%M in (%OUTMOUNTS%) do (
    git -C "%ROOT%\%%M" rev-parse "%VER%^{commit}" >nul 2>nul
    if errorlevel 1 (
        echo [ERROR] tag %VER% not found in %%M
        set "MISSING=1"
    )
)
if "%MISSING%"=="1" (
    echo [ERROR] update_platform: the top level changed nothing
    set "FAILED_LIST=!FAILED_LIST! platform_output (tag %VER%)"
    goto :delegate
)

rem  ---- checkout our own mounts; the first refusal stops the rest ----
set "FAILED=0"
for %%M in (%OUTMOUNTS%) do (
    if "!FAILED!"=="0" call :checkout_tag "%%M"
)

rem ============================================================
rem  Per-project mounts: call each owner's script with the SAME argument.
rem  No special case - projects that do not have this kind of mount answer
rem  through their default implementation.
rem ============================================================
:delegate
echo.
echo ============================================================
echo  Delegating the per-project mounts to their owners
echo ============================================================
for %%P in (%PROJS%) do (
    echo.
    echo ---- %%P -^> %VER% ----
    call "%ROOT%\%%P\scripts\update_platform.bat" %VER%
    if errorlevel 1 set "FAILED_LIST=!FAILED_LIST! %%P"
)

rem  The children are quiet (_CHILD_QUIET=1), so the consolidated verdict
rem  and the RESULT block below are the whole report of this run.
call :verdicts
if defined FAILED_LIST (
    echo.
    echo [ERROR] update_platform failed - see the [ERROR] lines above
    set "RC=1"
) else (
    echo.
    echo [OK] update_platform: everything aligned to %VER%
    echo.
    echo [info] re repo note: if the platform pointer changed, commit it in module\re
    echo        to bake this version ^(rollback just changes the working tree^).
    set "RC=0"
)
goto :epilogue

rem ---------------- helpers ----------------
:checkout_tag
set "M=%~1"
git -C "%ROOT%\%M%" checkout -q "%VER%"
if errorlevel 1 (
    echo [ERROR] checkout %VER% failed in %M% - dirty worktree? no change was discarded
    set "FAILED=1"
    set "FAILED_LIST=!FAILED_LIST! %M%"
    exit /b 1
)
for /f %%t in ('git -C "%ROOT%\%M%" describe --tags --abbrev^=0 2^>nul') do set "NOW=%%t"
echo   %M% : %NOW%
exit /b 0

:show
rem  Read-only view. The project scripts below are quiet (_CHILD_QUIET=1), so
rem  NOTHING on screen comes from them: this level prints its own mounts, a
rem  one-line header per project, then the consolidated verdict and the single
rem  RESULT block from :epilogue. The "To just look / To chase / To pin" hint
rem  block that used to sit here is gone on purpose - the RESULT block says
rem  the same thing, once.
echo.
echo ---- top level: platform mounts owned by this script ----
for %%M in (%OUTMOUNTS%) do call :show_one "%%M"
for %%P in (%PROJS%) do (
    echo.
    echo ---- %%P ----
    call "%ROOT%\%%P\scripts\update_platform.bat" show
    if errorlevel 1 set "FAILED_LIST=!FAILED_LIST! %%P"
)
call :verdicts
if defined FAILED_LIST (
    set "RC=1"
) else (
    set "RC=0"
)
goto :epilogue

rem ============================================================
rem  verdicts - one line per project, [OK] or [FAILED], printed after every
rem  child has run. FAILED_LIST is the single source of truth: it is also
rem  what _common.bat :summary prints as "Failed steps", so the verdict and
rem  the RESULT block can never disagree.
rem ============================================================
:verdicts
echo.
for %%P in (%PROJS%) do (
    set "V=[OK]"
    rem  Test membership only when the list exists: the "!F:x=!" replace form
    rem  is not expanded at all for an UNDEFINED variable, which would make
    rem  every project look failed on an all-green run.
    if defined FAILED_LIST (
        set "F=!FAILED_LIST!"
        if not "!F:%%P=!"=="!F!" set "V=[FAILED]"
    )
    echo  !V! %%P
)
exit /b 0

:show_one
set "M=%~1"
set "T=no tag"
if exist "%ROOT%\%M%\.git" (
    for /f %%t in ('git -C "%ROOT%\%M%" describe --tags --abbrev^=0 2^>nul') do set "T=%%t"
) else (
    set "T=not a repo"
)
echo   %M%  :  %T%
exit /b 0

:usage
echo.
echo Usage: update_platform.bat [^<tag^>^|latest^|show] [-h]
echo.
echo   no argument : align to the highest tag on origin/master ^(= latest^)
echo   2.0.1       : align to tag 2.0.1
echo   latest      : same as no argument
echo   show        : only show the current version, change nothing
echo.
echo   Owns ^(this level^): %MOWN%
echo   Other levels own theirs - this script does not run git on those.
echo.
echo   Non-destructive: a plain "git checkout" - a dirty worktree makes
echo   the run fail rather than being silently discarded.
echo.
echo Examples:
echo   update_platform.bat
echo   update_platform.bat 2.0.1
echo   update_platform.bat show
echo.
set "RC=0"
goto :epilogue

rem ============================================================
rem  Single exit point: put the caller's hold wish back (the "1" we set to
rem  silence the child scripts must not silence our own pause), print the
rem  RESULT block, then hold. See _common.bat :hold.
rem ============================================================
:epilogue
set "_SCRIPT_NO_HOLD=%_HOLD_IN%"
call "%HERE%private_scripts\_common.bat" summary %RC%
exit /b %RC%
