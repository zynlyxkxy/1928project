@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  sync.bat - cross-project sync, user entry.
rem
rem    sync.bat                 every edge (A1..A9)
rem    sync.bat check          read-only report, writes nothing
rem    sync.bat re mctrl       push ONLY the out-edges of these sources
rem    sync.bat gui            gui is a pure consumer - nothing to push
rem    sync.bat --dry-run      report only (combine with any of the above)
rem    sync.bat -h             this help + the source table
rem
rem  Semantics: naming a project makes it the SOURCE - its content is
rem  pushed downstream. To refresh a consumer's snapshots, sync its
rem  upstreams (or run the full sync).
rem
rem  Edges (one table, engine: engineering\scripts\private_scripts\_sync_ops.bat):
rem    io     : A1 io protocol        -^> mctrl
rem    motion : A2 motion protocol    -^> mctrl
rem    re     : A3 rm protocol + A4/A5 ReData -> mctrl + gui
rem    mctrl  : A6 MC schema -> config, A7 protocol + A8 McData -> gui
rem    gui    : none (last consumer)
rem
rem  Identical content keeps the target mtime (no needless rebuild);
rem  directory edges mirror (files deleted upstream disappear here).
rem  Syncing makes the downstream worktree dirty on purpose - commit
rem  the snapshots there.
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

set "DRY="
set "SRCS="
:parse
if "%~1"=="" goto :parse_done
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage
if /i "%~1"=="check" (
    call "%HERE%private_scripts\_sync_ops.bat" check
    set "RC=!ERRORLEVEL!"
    if not "!RC!"=="0" set "FAILED_LIST=check"
    goto :epilogue
)
if /i "%~1"=="--dry-run" (
    set "DRY=--dry-run"
) else (
    set "SRCS=!SRCS! %~1"
)
shift
goto :parse
:parse_done

if "%SRCS%"=="" (
    call "%HERE%private_scripts\_sync_ops.bat" all %DRY%
    set "RC=!ERRORLEVEL!"
    if not "!RC!"=="0" set "FAILED_LIST=sync all"
    goto :epilogue
)

rem  Every source is attempted; the epilogue lists the ones that failed.
set "FAILED=0"
set "FAILED_LIST="
for %%S in (%SRCS%) do (
    call :is_src "%%S"
    if errorlevel 1 (
        echo [ERROR] sync.bat: "%%S" is not a sync source - try -h
        set "FAILED=1"
        set "FAILED_LIST=!FAILED_LIST! %%~S"
    ) else (
        call "%HERE%private_scripts\_sync_ops.bat" push %%S %DRY%
        if errorlevel 1 (
            set "FAILED=1"
            set "FAILED_LIST=!FAILED_LIST! %%~S"
        )
    )
)
set "RC=!FAILED!"
goto :epilogue

:is_src
for %%K in (io motion re mctrl gui) do if /i "%~1"=="%%K" exit /b 0
exit /b 1

:usage
echo.
echo Usage: sync.bat [--dry-run] [source...] [-h]
echo.
echo   no arguments : run every edge ^(A1..A9^)
echo   check        : read-only report, writes nothing
echo   source...    : push only the out-edges of these projects
echo   --dry-run    : report only, write nothing
echo.
echo   Sources and what they push:
echo     io      A1  io protocol headers            -^> mctrl
echo     motion  A2  motion protocol headers        -^> mctrl
echo     re      A3  rm protocol headers            -^> mctrl
echo             A4  ReData                         -^> mctrl
echo             A5  ReData                         -^> gui
echo     mctrl   A6  MC_WM_Schema.json              -^> config\mc_config
echo             A7  mctrl protocol headers         -^> gui
echo             A8  McData                         -^> gui
echo     gui     none - pure consumer, nothing to push
echo.
echo Examples:
echo   sync.bat
echo   sync.bat re
echo   sync.bat io motion --dry-run
echo   sync.bat check
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
