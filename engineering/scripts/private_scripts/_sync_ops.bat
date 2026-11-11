@echo off
rem ============================================================
rem  Cross-project sync engine. One table, one implementation, at the top
rem  level - the sub-projects only forward here.
rem
rem    sync_ops.bat check                 read-only report of A1..A9
rem    sync_ops.bat proto io|motion|rm    protocol headers -> mctrl
rem    sync_ops.bat proto mctrl           mctrl protocol headers -> gui
rem    sync_ops.bat data  re|mctrl        data dirs / schema -> mctrl + gui
rem    sync_ops.bat cmake                 shared build framework -> every sub-project
rem    sync_ops.bat proj  <proj>          every edge touching <proj>
rem    sync_ops.bat push  <proj>          out-edges of <proj> only
rem    sync_ops.bat all                   every edge
rem    --dry-run                          report only, change nothing
rem
rem  The edge table (all of it):
rem    A1 proto io     -> mctrl\MainProject\RPCModule\include\io\
rem    A2 proto motion -> mctrl\MainProject\RPCModule\include\motion\
rem    A3 proto rm     -> mctrl\MainProject\RPCModule\include\rm\
rem    A4 data  re     -> mctrl\MainProject\RecipeModule\ReData\      (mirror)
rem    A5 data  re     -> gui\MainProject\MCRecipeModule\ReData\      (mirror)
rem    A6 data  mctrl  -> engineering\config\mc_config\MachineConstants\  (JSON schema)
rem    A7 proto mctrl  -> gui\MainProject\RPCModule\include\
rem    A8 data  mctrl  -> gui\MainProject\MCRecipeModule\McData\      (mirror)
rem    A9 cmake        to each sub-project's cmake dir                 (mirror; the
rem                       canonical source is engineering\cmake\, so every
rem                       sub-project builds standalone from its own copy)
rem
rem  Every copy is content-compared: an unchanged target keeps its mtime, so
rem  downstream rebuilds are not needlessly triggered. Mirror edges delete
rem  files that no longer exist in the source.
rem
rem  ASCII-only on purpose: see the note in engineering\scripts\private_scripts\_common.bat.
rem ============================================================
setlocal enabledelayedexpansion
set "HERE=%~dp0"
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
rem  Never stop here, and never let anything we call stop either: this file
rem  is only ever driven by an entry script (sync.bat / build_exe.bat). See
rem  the hold rules in _common.bat.
set "_SCRIPT_NO_HOLD=1"

set "CMD=%~1"
if "%CMD%"=="" (
    echo [ERROR] sync_ops.bat: missing command. Try: check ^| proto ^| data ^| proj ^| push ^| all
    exit /b 1
)
shift

set "DRY="
set "ARG="
:parse
if "%~1"=="" goto :parse_done
if /i "%~1"=="--dry-run" (
    set "DRY=-DryRun"
) else (
    set "ARG=%~1"
)
shift
goto :parse
:parse_done

set /a UPD=0
set /a SKIP=0
set /a DEL=0
set /a FAIL=0

if /i "%CMD%"=="check" goto :check
if /i "%CMD%"=="proto" goto :proto
if /i "%CMD%"=="data"  goto :data
if /i "%CMD%"=="cmake" goto :cmake
if /i "%CMD%"=="proj"  goto :proj
if /i "%CMD%"=="push"  goto :push
if /i "%CMD%"=="all"   goto :all
echo [ERROR] sync_ops.bat: unknown command "%CMD%"
exit /b 1

rem ============================================================
rem  check - read-only: source present? target current?
rem ============================================================
:check
set "DRY=-DryRun"
echo [check] A1..A9  ^(this mode writes nothing^)
call :edge_a1
call :edge_a2
call :edge_a3
call :edge_a4
call :edge_a5
call :edge_a6
call :edge_a7
call :edge_a8
call :edge_a9
goto :tail

:proto
if /i "%ARG%"=="io"     goto :proto_io
if /i "%ARG%"=="motion" goto :proto_motion
if /i "%ARG%"=="rm"     goto :proto_rm
if /i "%ARG%"=="mctrl"  goto :proto_mctrl
echo [ERROR] sync_ops proto: expected io ^| motion ^| rm ^| mctrl
exit /b 1
:proto_io
call :edge_a1
goto :tail
:proto_motion
call :edge_a2
goto :tail
:proto_rm
call :edge_a3
goto :tail
:proto_mctrl
call :edge_a7
goto :tail

:data
if /i "%ARG%"=="re"    goto :data_re
if /i "%ARG%"=="mctrl" goto :data_mctrl
echo [ERROR] sync_ops data: expected re ^| mctrl
exit /b 1
:data_re
call :edge_a4
call :edge_a5
goto :tail
:data_mctrl
call :edge_a6
call :edge_a8
goto :tail

rem  cmake = the shared build framework (canonical: engineering\cmake\) into
rem  every sub-project's own cmake\ dir. Not tied to any single project, so
rem  it is its own command; check/all also carry it.
:cmake
call :edge_a9
goto :tail

:all
call :edge_a1
call :edge_a2
call :edge_a3
call :edge_a4
call :edge_a5
call :edge_a6
call :edge_a7
call :edge_a8
call :edge_a9
goto :tail

rem  push = out-edges of one project (what it publishes to others)
:push
if /i "%ARG%"=="io"     goto :push_io
if /i "%ARG%"=="motion" goto :push_motion
if /i "%ARG%"=="re"     goto :push_re
if /i "%ARG%"=="mctrl"  goto :push_mctrl
if /i "%ARG%"=="gui"    goto :push_gui
echo [ERROR] sync_ops push: expected io ^| motion ^| re ^| mctrl ^| gui
exit /b 1
:push_io
call :edge_a1
goto :tail
:push_motion
call :edge_a2
goto :tail
:push_re
call :edge_a3
call :edge_a4
call :edge_a5
goto :tail
:push_mctrl
call :edge_a6
call :edge_a7
call :edge_a8
goto :tail
:push_gui
echo [sync] gui is the last consumer - it has no out-edges
goto :tail

rem  proj = in-edges + out-edges of one project
:proj
if /i "%ARG%"=="io"     goto :push_io
if /i "%ARG%"=="motion" goto :push_motion
if /i "%ARG%"=="re"     goto :push_re
if /i "%ARG%"=="mctrl"  goto :proj_mctrl
if /i "%ARG%"=="gui"    goto :proj_gui
echo [ERROR] sync_ops proj: expected io ^| motion ^| re ^| mctrl ^| gui
exit /b 1
:proj_mctrl
call :edge_a1
call :edge_a2
call :edge_a3
call :edge_a4
call :edge_a6
call :edge_a7
call :edge_a8
goto :tail
:proj_gui
call :edge_a5
call :edge_a7
call :edge_a8
goto :tail

:tail
echo.
echo   updated %UPD%, unchanged %SKIP%, deleted %DEL%, problems %FAIL%
set "SUMMARY_NOTE=sync %CMD% %ARG%: updated %UPD%, unchanged %SKIP%, deleted %DEL%"
if not "%FAIL%"=="0" (
    echo [ERROR] sync_ops: %FAIL% edge^(s^) had a missing source or failed - nothing else was touched
    exit /b 1
)
exit /b 0

rem ============================================================
rem  Edges - one label per edge, nothing else
rem ============================================================
:edge_a1
call :sect "A1  proto io      to  mctrl\MainProject\RPCModule\include\io"
call :one "%ROOT%\module\io\interface\include\io_controller.h" "%ROOT%\mctrl\MainProject\RPCModule\include\io\io_controller.h"
call :one "%ROOT%\module\io\interface\include\ioc_data.h"      "%ROOT%\mctrl\MainProject\RPCModule\include\io\ioc_data.h"
exit /b 0

:edge_a2
call :sect "A2  proto motion  to  mctrl\MainProject\RPCModule\include\motion"
call :one "%ROOT%\module\motion\interface\include\motion_controller.h" "%ROOT%\mctrl\MainProject\RPCModule\include\motion\motion_controller.h"
call :one "%ROOT%\module\motion\interface\include\motionc_data.h"      "%ROOT%\mctrl\MainProject\RPCModule\include\motion\motionc_data.h"
exit /b 0

:edge_a3
call :sect "A3  proto rm      to  mctrl\MainProject\RPCModule\include\rm"
call :one "%ROOT%\module\re\platform\RM\api\rm_controller.h" "%ROOT%\mctrl\MainProject\RPCModule\include\rm\rm_controller.h"
call :one "%ROOT%\module\re\platform\RM\api\rmc_data.h"      "%ROOT%\mctrl\MainProject\RPCModule\include\rm\rmc_data.h"
exit /b 0

:edge_a4
call :sect "A4  data  re      to  mctrl\MainProject\RecipeModule\ReData"
call :dir "%ROOT%\module\re\MainProject\ReData" "%ROOT%\mctrl\MainProject\RecipeModule\ReData"
exit /b 0

:edge_a5
call :sect "A5  data  re      to  gui\MainProject\MCRecipeModule\ReData"
call :dir "%ROOT%\module\re\MainProject\ReData" "%ROOT%\gui\MainProject\MCRecipeModule\ReData"
exit /b 0

:edge_a6
call :sect "A6  data  mctrl   to  config\mc_config\MachineConstants"
call :one "%ROOT%\mctrl\config\MC_WM_Schema.json" "%ROOT%\engineering\config\mc_config\MachineConstants\MC_WM_Schema.json"
exit /b 0

:edge_a7
call :sect "A7  proto mctrl   to  gui\MainProject\RPCModule\include"
call :one "%ROOT%\mctrl\MainProject\RPCModule\include\mctrl\mctrl_controller.h" "%ROOT%\gui\MainProject\RPCModule\include\mctrl_controller.h"
call :one "%ROOT%\mctrl\MainProject\RPCModule\include\mctrl\mctrlc_data.h"      "%ROOT%\gui\MainProject\RPCModule\include\mctrlc_data.h"
exit /b 0

:edge_a8
call :sect "A8  data  mctrl   to  gui\MainProject\MCRecipeModule\McData"
call :dir "%ROOT%\mctrl\MainProject\MCModule\McData" "%ROOT%\gui\MainProject\MCRecipeModule\McData"
exit /b 0

rem  A9: the shared build framework (istar_*.cmake) is committed inside every
rem  sub-project so each one builds standalone. This mirrors the canonical
rem  source 1927project\cmake\ into each sub-project's cmake\ dir. Mirror
rem  semantics: the target holds exactly these files, nothing project-local.
:edge_a9
call :sect "A9  cmake framework to  every sub-project cmake"
call :dir "%ROOT%\engineering\cmake" "%ROOT%\gui\cmake"
call :dir "%ROOT%\engineering\cmake" "%ROOT%\mctrl\cmake"
call :dir "%ROOT%\engineering\cmake" "%ROOT%\module\io\cmake"
call :dir "%ROOT%\engineering\cmake" "%ROOT%\module\motion\cmake"
call :dir "%ROOT%\engineering\cmake" "%ROOT%\module\re\cmake"
exit /b 0

rem ============================================================
rem  helpers
rem ============================================================
:sect
echo.
echo [sync] %~1
exit /b 0

rem  one file: content-compare copy, counts [OK]/[skip], [MISS] = failure.
rem
rem  Counting is EXACTLY what the old find /C against the temp log did: one
rem  boolean per comparison (i.e. per :one call), so a [skip] line adds 1 to
rem  SKIP and an [OK] and/or [diff] line adds 1 to UPD each. Success comes
rem  from the "[sync] rc=" sentinel the .ps1 prints; a run that prints no
rem  sentinel is a failure. Nothing is written to disk any more.
:one
if not exist "%~1" (
    if not exist "%~2" (
        echo   [SKIP] not present on either side: %~nx1
        exit /b 0
    )
    echo   [MISS] source not found: %~1
    set /a FAIL+=1
    exit /b 1
)
set "HAS_SKIP=0"
set "HAS_OK=0"
set "HAS_DIFF=0"
set "SRC_RC="
rem  The command inside for /f runs in a child cmd, so its exit code cannot be
rem  used - the sentinel line is. Do not test the sentinel with the
rem  !VAR:[sync] rc=0=! substitution: cmd splits that at the first "=", so the
rem  search string stops at "rc" (both sentinels would match). The sentinel is
rem  always "[sync] rc=<digit>", so its LAST character decides.
for /f "delims=" %%L in ('powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%_sync_proto.ps1" -Src "%~1" -Dst "%~2" %DRY% 2^>^&1') do (
    echo(%%L
    set "LINE=%%L"
    if not "!LINE!"=="!LINE:[skip]=!" set "HAS_SKIP=1"
    if not "!LINE!"=="!LINE:[OK]=!"   set "HAS_OK=1"
    if not "!LINE!"=="!LINE:[diff]=!" set "HAS_DIFF=1"
    if not "!LINE!"=="!LINE:[sync] rc=!" (
        if "!LINE:~-1!"=="0" set "SRC_RC=0"
        if "!LINE:~-1!"=="1" set "SRC_RC=1"
    )
)
if not defined SRC_RC (
    echo   [ERROR] sync_proto printed no "[sync] rc=" line - treated as failure
    set /a FAIL+=1
    exit /b 1
)
if not "!SRC_RC!"=="0" (
    set /a FAIL+=1
    exit /b 1
)
if "!HAS_SKIP!"=="1" set /a SKIP+=1
if "!HAS_OK!"=="1"   set /a UPD+=1
if "!HAS_DIFF!"=="1" set /a UPD+=1
exit /b 0

rem  one directory: mirror sync, counts [OK]/[diff] as updates and [DEL]/[stale]
rem  as deletes. Same per-comparison counting and same "[sync] rc=" gate as
rem  :one above - see the note there.
:dir
if not exist "%~1" (
    echo   [MISS] source dir not found: %~1
    set /a FAIL+=1
    exit /b 1
)
if not exist "%~2" (
    echo   [INFO] target dir does not exist yet: %~2
    if not defined DRY mkdir "%~2" 2>nul
)
set "HAS_SKIP=0"
set "HAS_OK=0"
set "HAS_DIFF=0"
set "HAS_DEL=0"
set "HAS_STALE=0"
set "SRC_RC="
for /f "delims=" %%L in ('powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%_sync_dir.ps1" -Src "%~1" -Dst "%~2" %DRY% 2^>^&1') do (
    echo(%%L
    set "LINE=%%L"
    if not "!LINE!"=="!LINE:[skip]=!"  set "HAS_SKIP=1"
    if not "!LINE!"=="!LINE:[OK]=!"    set "HAS_OK=1"
    if not "!LINE!"=="!LINE:[diff]=!"  set "HAS_DIFF=1"
    if not "!LINE!"=="!LINE:[DEL]=!"   set "HAS_DEL=1"
    if not "!LINE!"=="!LINE:[stale]=!" set "HAS_STALE=1"
    if not "!LINE!"=="!LINE:[sync] rc=!" (
        if "!LINE:~-1!"=="0" set "SRC_RC=0"
        if "!LINE:~-1!"=="1" set "SRC_RC=1"
    )
)
if not defined SRC_RC (
    echo   [FAIL] sync_dir printed no "[sync] rc=" line - treated as failure
    set /a FAIL+=1
    exit /b 1
)
if not "!SRC_RC!"=="0" (
    echo   [FAIL] sync_dir failed: %~1
    set /a FAIL+=1
    exit /b 1
)
if "!HAS_SKIP!"=="1"  set /a SKIP+=1
if "!HAS_OK!"=="1"    set /a UPD+=1
if "!HAS_DIFF!"=="1"  set /a UPD+=1
if "!HAS_DEL!"=="1"   set /a DEL+=1
if "!HAS_STALE!"=="1" set /a DEL+=1
exit /b 0
