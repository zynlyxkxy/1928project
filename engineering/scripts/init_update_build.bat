@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  init_update_build.bat - one click: restore the submodule versions this
rem  commit records, then build the package.
rem
rem    init_update_build.bat              submodules -> build_exe
rem    init_update_build.bat --no-build   stop after the submodule restore
rem    init_update_build.bat -h           this help
rem
rem  Chain (each step must succeed or the chain stops):
rem    1. init_submodules.bat   every submodule -> the SHA this commit records
rem    2. build_exe.bat         build 5 projects + sync + assemble exe\
rem
rem  This chain does NOT move versions. Upgrading is explicit and manual:
rem    update_corecode.bat / update_platform.bat / update_thirdparty.bat
rem  After those, commit the new submodule pointers in 1928 - otherwise the
rem  commit no longer describes what you built.
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

set "DO_BUILD=1"
:parse
if "%~1"=="" goto :parse_done
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage
if /i "%~1"=="--no-build" (
    set "DO_BUILD=0"
) else (
    echo [ERROR] init_update_build.bat: unexpected argument "%~1" - try -h
    exit /b 1
)
shift
goto :parse
:parse_done

echo.
echo ============================================================
echo  init_update_build   build=%DO_BUILD%   ^(versions = this commit^)
echo ============================================================

echo.
echo ============ [1/2] submodules -^> recorded SHAs ============
call "%HERE%init_submodules.bat"
if errorlevel 1 (
    set "FAILED_LIST=init_submodules"
    goto :chain_failed
)

if "%DO_BUILD%"=="0" (
    echo.
    echo [OK] init_update_build done ^(no build requested^):
    echo       submodules restored to the versions this commit records.
    echo       Build whenever ready:  engineering\scripts\build_exe.bat
    set "RC=0"
    goto :epilogue
)

echo.
echo ============ [2/2] build + assemble exe\ ============
call "%HERE%build_exe.bat"
if errorlevel 1 (
    set "FAILED_LIST=build_exe"
    goto :chain_failed
)

echo.
echo ============================================================
echo  [OK] init_update_build complete: exe\ rebuilt from this commit.
echo ============================================================
set "RC=0"
goto :epilogue

rem  Fail fast: the first failing step stops the chain and is named in the
rem  RESULT block below.
:chain_failed
echo.
echo [ERROR] init_update_build stopped - failing step: %FAILED_LIST%
set "RC=1"
goto :epilogue

:usage
echo.
echo Usage: init_update_build.bat [--no-build] [-h]
echo.
echo   no argument : restore submodules to this commit's SHAs, then build
echo   --no-build  : only restore the submodules
echo.
echo   Chain: init_submodules -^> build_exe. Any failing step stops it.
echo   Versions are NOT moved here: use update_corecode.bat /
echo   update_platform.bat / update_thirdparty.bat when you mean to upgrade,
echo   then commit the new submodule pointers in 1928.
echo.
echo Examples:
echo   init_update_build.bat
echo   init_update_build.bat --no-build
echo.
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
