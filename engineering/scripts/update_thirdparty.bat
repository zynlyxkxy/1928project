@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  update_thirdparty.bat - pull the THIRD-PARTY repositories.
rem
rem    update_thirdparty.bat           pull every third-party repo (= latest)
rem    update_thirdparty.bat latest    same as no argument
rem    update_thirdparty.bat show      show the current version, change nothing
rem    update_thirdparty.bat -h        this help
rem
rem  Ownership, "who owns it, maintains it":
rem    this level : simulator (simulator.git, branch develop) - the ONLY
rem                 repo this script runs git on.
rem    delegated  : mctrl\third_party, plus the projects that own no
rem                 third-party repo at all - each project's own script
rem                 is called with the same argument instead.
rem
rem  NON-destructive: fetch -> checkout develop -> pull (fast-forward only).
rem  Nothing is discarded: a dirty or diverged worktree makes the run fail with
rem  a non-zero exit and the local changes still in place - fix it, re-run.
rem
rem  simulator used to be pulled by update_corecode.bat as one more "code repo".
rem  It is a third-party repo, so it lives here now: a one-click run used to
rem  pull the same upstream twice.
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

set "MOWN=simulator"
set "SUB=simulator"
set "PROJS=module\io module\motion module\re mctrl gui"
set "FAILED_LIST="

set "ARG=%~1"
if /i "%ARG%"=="-h" goto :usage
if /i "%ARG%"=="/?" goto :usage
if /i "%ARG%"=="show" goto :show
if "%ARG%"=="" set "ARG=latest"
if /i not "%ARG%"=="latest" (
    echo [ERROR] update_thirdparty.bat: unexpected argument "%ARG%" - third-party has no tag semantics
    echo         run "update_thirdparty.bat -h" for the contract
    set "RC=1"
    goto :epilogue
)

where git >nul 2>nul
if errorlevel 1 (
    echo [ERROR] no git found
    set "RC=1"
    goto :epilogue
)

echo.
echo ---- top level: %SUB% ----
echo [1/3] init if missing, then fetch origin develop ...
if not exist "%ROOT%\%SUB%\.git" (
    echo       %SUB% is not initialized - initializing it first
    git -C "%ROOT%" submodule update --init "%SUB%" || (
        echo [ERROR] failed to init submodule %SUB%
        set "FAILED_LIST=!FAILED_LIST! %SUB%"
        goto :delegate
    )
)
git -C "%ROOT%\%SUB%" fetch origin develop || (
    echo [ERROR] %SUB%: git fetch origin develop failed - network or credentials?
    set "FAILED_LIST=!FAILED_LIST! %SUB%"
    goto :delegate
)

echo [2/3] checkout develop ...
git -C "%ROOT%\%SUB%" checkout develop 2>nul
if errorlevel 1 git -C "%ROOT%\%SUB%" checkout -b develop origin/develop 2>nul
rem  Both attempts are silent: the failure is reported once, below, after we
rem  look at where HEAD actually ended up. A fresh submodule clone has no
rem  local develop yet; a dirty worktree cannot be moved at all.
set "SIM_BR="
for /f "delims=" %%b in ('git -C "%ROOT%\%SUB%" rev-parse --abbrev-ref HEAD 2^>nul') do set "SIM_BR=%%b"
if /i not "!SIM_BR!"=="develop" (
    echo [ERROR] %SUB%: cannot get onto develop ^(HEAD is "!SIM_BR!"^) - dirty worktree? no change was discarded
    set "FAILED_LIST=!FAILED_LIST! %SUB%"
    goto :delegate
)

echo [3/3] pull --ff-only origin develop ...
git -C "%ROOT%\%SUB%" pull --ff-only origin develop || (
    echo [ERROR] %SUB%: pull failed - dirty or diverged worktree? no change was discarded
    set "FAILED_LIST=!FAILED_LIST! %SUB%"
    goto :delegate
)
for /f "delims=" %%r in ('git -C "%ROOT%\%SUB%" log -1 --oneline 2^>nul') do set "SIM_HEAD=%%r"
echo [OK] %SUB% on develop: !SIM_HEAD!

rem ============================================================
rem  Per-project third-party repos: call each owner's script with the SAME
rem  argument. Projects that own no third-party repo answer through their
rem  default implementation - a no-op that returns 0 - so there is no
rem  special case here.
rem ============================================================
:delegate
echo.
echo ============================================================
echo  Delegating the per-project third-party repos to their owners
echo ============================================================
for %%P in (%PROJS%) do (
    echo.
    echo ---- %%P -^> %ARG% ----
    call "%ROOT%\%%P\scripts\update_thirdparty.bat" %ARG%
    if errorlevel 1 set "FAILED_LIST=!FAILED_LIST! %%P"
)

rem  The children are quiet (_CHILD_QUIET=1), so the consolidated verdict
rem  and the RESULT block below are the whole report of this run.
call :verdicts
if defined FAILED_LIST (
    echo.
    echo [ERROR] update_thirdparty failed - see the [ERROR] lines above
    set "RC=1"
) else (
    echo.
    echo [OK] update_thirdparty: every third-party repo is on develop
    set "RC=0"
)
goto :epilogue

:show
rem  Read-only view. The project scripts below are quiet (_CHILD_QUIET=1), so
rem  NOTHING on screen comes from them: this level prints its own repo, a
rem  one-line header per project, then the consolidated verdict and the single
rem  RESULT block from :epilogue. The "To just look / To chase" hint block that
rem  used to sit here is gone on purpose - the RESULT block says the same
rem  thing, once.
echo.
echo ---- top level: third-party repo owned by this script ----
set "B=n/a"
set "H="
if exist "%ROOT%\%SUB%\.git" (
    for /f %%b in ('git -C "%ROOT%\%SUB%" rev-parse --abbrev-ref HEAD 2^>nul') do set "B=%%b"
    for /f %%h in ('git -C "%ROOT%\%SUB%" rev-parse --short HEAD 2^>nul') do set "H=%%h"
)
echo   %SUB%  :  %B%  %H%
for %%P in (%PROJS%) do (
    echo.
    echo ---- %%P ----
    call "%ROOT%\%%P\scripts\update_thirdparty.bat" show
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

:usage
echo.
echo Usage: update_thirdparty.bat [latest^|show] [-h]
echo.
echo   no argument : pull the third-party submodule ^(= latest^)
echo   latest      : same as no argument
echo   show        : only show the current version, change nothing
echo.
echo   Owns ^(this level^): %MOWN%
echo   Other levels own theirs - this script does not run git on those.
echo   A dirty or diverged worktree fails the run - no change is discarded.
echo.
echo Examples:
echo   update_thirdparty.bat
echo   update_thirdparty.bat show
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
