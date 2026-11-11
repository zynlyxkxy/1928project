@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  init_submodules.bat - put every submodule at the version THIS commit
rem  records (one 1928 commit == one fixed set of submodule SHAs).
rem
rem    init_submodules.bat      sync + update --init --recursive
rem    init_submodules.bat -h   this help
rem
rem  Version policy (the whole point of this script):
rem    init_submodules.bat  = RESTORE   (git submodule update, pinned SHAs)
rem    update_*.bat         = UPGRADE   (checkout develop / tag; afterwards
rem                            you commit the new pointers in 1928 yourself)
rem  So a build from a 1928 commit is reproducible: this script never pulls
rem  and never chooses a version.
rem
rem  No -f: a submodule with local changes makes the update fail and the
rem  tree is left as it was - nothing is discarded behind your back.
rem
rem  ASCII-only on purpose.
rem ============================================================
set "HERE=%~dp0"
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
rem  Hold policy (family convention): the caller's wish decides whether OUR
rem  window stops, so it is saved here and put back before the epilogue.
set "_HOLD_IN=%_SCRIPT_NO_HOLD%"
set "_SCRIPT_NO_HOLD=1"

if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage
if not "%~1"=="" (
    echo [ERROR] init_submodules.bat: unexpected argument "%~1" - try -h
    exit /b 1
)

where git >nul 2>nul
if errorlevel 1 (
    echo [ERROR] no git found
    exit /b 1
)

echo.
echo ============================================================
echo  submodules: align every level to the SHA recorded by this commit
echo ============================================================

echo.
echo ---- [1/2] submodule sync --recursive (URLs from .gitmodules) ----
git -C "%ROOT%" submodule sync --recursive
if errorlevel 1 (
    echo [ERROR] git submodule sync failed - check the remote URLs
    set "RC=1"
    goto :epilogue
)

echo.
echo ---- [2/2] submodule update --init --recursive (pinned SHAs) ----
git -C "%ROOT%" submodule update --init --recursive
if errorlevel 1 (
    echo [ERROR] submodule update failed.
    echo         Usual causes: local changes inside a submodule, missing
    echo         network/auth, or a stale commit. Nothing was forced - the
    echo         tree is as it was. Commit/stash inside that submodule and
    echo         re-run.
    set "RC=1"
    goto :epilogue
)

echo.
echo ---- submodule state ----
echo  ^(prefix: space = matches this commit, + = different SHA, - = missing^)
git -C "%ROOT%" submodule status

git -C "%ROOT%" submodule status | findstr /b /c:"+" /c:"-" /c:"U" >nul
if not errorlevel 1 (
    echo.
    echo [WARN] some submodules are NOT at the SHA this commit records -
    echo        that is the state the update_*.bat tools leave behind.
    echo        Commit the new pointers in 1928 if you want this build to
    echo        be reproducible from the commit.
)

echo.
echo [OK] init_submodules done - every submodule is at the recorded version.
set "RC=0"
goto :epilogue

rem ============================================================
rem  Single exit point: put the caller's hold wish back, print the RESULT
rem  block, then hold. See _common.bat :hold.
rem ============================================================
:epilogue
set "_SCRIPT_NO_HOLD=%_HOLD_IN%"
call "%HERE%private_scripts\_common.bat" summary %RC%
exit /b %RC%

:usage
echo.
echo Usage: init_submodules.bat [-h]
echo.
echo   git submodule sync --recursive  +  git submodule update --init --recursive
echo   i.e. restore every submodule (all nesting levels) to the SHA this
echo   1928 commit records. Never pulls, never picks a version.
echo.
echo   Upgrading a repo is a separate, explicit step:
echo     update_corecode.bat / update_platform.bat / update_thirdparty.bat
echo   After running those, commit the new submodule pointers in 1928 -
echo   otherwise the commit no longer describes what you built.
echo.
echo Examples:
echo   init_submodules.bat
echo.
set "RC=0"
goto :epilogue
