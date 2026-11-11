@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  build_exe.bat - build everything and (re)generate exe\.
rem
rem    build_exe.bat                          release, all five projects
rem    build_exe.bat debug                    debug, all
rem    build_exe.bat io motion                release, only these: builds them
rem                                          and mirrors ONLY their folders
rem                                          into exe\apps\ (config\, platform\,
rem                                          shared\ and the other apps are
rem                                          left untouched)
rem    build_exe.bat -c gui                   clean + build gui
rem    build_exe.bat --no-sync                skip the cross-project sync
rem                                          (in-place debugging: do not
rem                                           dirty the downstream repos)
rem    build_exe.bat --no-verify              skip the final verify
rem    build_exe.bat pack [ver]               zip exe\ -> dist\1927-exe-<ver>.zip
rem    build_exe.bat status                   exe\ overview + versions
rem    build_exe.bat -h                       this help
rem
rem  Two deployment modes:
rem    all five selected (default) -> full assemble: exe\apps\ + platform\ +
rem      shared\ are rebuilt and config\ is refreshed, then verify runs.
rem    a subset selected           -> per-app mirror only (exe\apps\<name>),
rem      verify is skipped: exe\ is not a complete package in that mode.
rem
rem  Zero git here: this script NEVER pulls, checks out or commits.
rem  Version alignment is update_corecode.bat / update_platform.bat.
rem
rem  Order is a hard constraint (consumers must be synced before they
rem  are built):
rem    io -> motion -> simbridge -> re -> sync A1..A5 -> mctrl
rem    -> sync A6..A8 -> gui -> assemble exe\ -> verify
rem
rem  simbridge = the simulation relay (simulator\simbridge\relay, inside the
rem  simulator submodule). It links module\io + module\motion
rem  output\interface\rpc, so it is built right after motion. It is
rem  release-only and it assembles its own runtime tree
rem  (simulator\simbridge\deploy\bin via deploy\rebuild_bin.bat);
rem  it contributes nothing to exe\.
rem
rem  ASCII-only on purpose.
rem ============================================================
set "HERE=%~dp0"
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
rem  Hold policy: _SCRIPT_NO_HOLD=1 tells the scripts CALLED below not to
rem  stop; our own RESULT block must still stop, so the caller's value is
rem  saved here and put back before we hold (see :epilogue_hold and
rem  _common.bat :hold).
set "_HOLD_IN=%_SCRIPT_NO_HOLD%"
set "_SCRIPT_NO_HOLD=1"

set "CMD=%~1"
if /i "%CMD%"=="-h" goto :usage
if /i "%CMD%"=="/?" goto :usage
rem  pack/status delegate to _deploy_ops.bat and keep its output as-is (no
rem  RESULT block of their own); they still hold at the end like the build
rem  path. EPIRC is the exit code :epilogue_hold uses.
if /i "%CMD%"=="pack" (
    call "%HERE%private_scripts\_deploy_ops.bat" pack %2 %3
    set "EPIRC=!ERRORLEVEL!"
    goto :epilogue_hold
)
if /i "%CMD%"=="status" (
    call "%HERE%private_scripts\_deploy_ops.bat" status
    set "EPIRC=!ERRORLEVEL!"
    goto :epilogue_hold
)

set "DO_RELEASE=0"
set "DO_DEBUG=0"
set "DO_CLEAN=0"
set "DO_SYNC=1"
set "DO_VERIFY=1"
set "LIST="

:parse
if "%~1"=="" goto :parse_done
if /i "%~1"=="release"      set "DO_RELEASE=1"
if /i "%~1"=="debug"        set "DO_DEBUG=1"
if /i "%~1"=="-c"           set "DO_CLEAN=1"
if /i "%~1"=="clean"        set "DO_CLEAN=1"
if /i "%~1"=="--no-sync"    set "DO_SYNC=0"
if /i "%~1"=="--no-verify"  set "DO_VERIFY=0"
if /i "%~1"=="io"        set "LIST=!LIST! io"
if /i "%~1"=="motion"    set "LIST=!LIST! motion"
if /i "%~1"=="simbridge" set "LIST=!LIST! simbridge"
if /i "%~1"=="re"        set "LIST=!LIST! re"
if /i "%~1"=="mctrl"     set "LIST=!LIST! mctrl"
if /i "%~1"=="gui"       set "LIST=!LIST! gui"
if /i "%~1"=="all"       set "LIST=io motion simbridge re mctrl gui"
set "KNOWN=0"
for %%K in (release debug -c clean --no-sync --no-verify all io motion simbridge re mctrl gui) do if /i "%~1"=="%%K" set "KNOWN=1"
if "!KNOWN!"=="0" (
    echo [ERROR] build_exe.bat: unknown argument "%~1" - try -h
    exit /b 1
)
shift
goto :parse
:parse_done
if "%DO_RELEASE%%DO_DEBUG%"=="00" set "DO_RELEASE=1"

rem canonical order no matter how the user listed the projects
set "ORDER=io motion simbridge re mctrl gui"
if "%LIST%"=="" set "LIST=%ORDER%"
set "SEL="
for %%O in (%ORDER%) do (
    for %%L in (%LIST%) do if /i "%%O"=="%%L" set "SEL=!SEL! %%O"
)

rem  FULLSET = all five app projects selected. Then exe\ is rebuilt from
rem  scratch (assemble + verify). A subset only mirrors the selected app
rem  folders into exe\apps\ and leaves platform\, shared\, config\ and the
rem  other apps untouched - that is the "I fixed mctrl, update only mctrl"
rem  path.
set "APPCOUNT=0"
for %%O in (io motion re mctrl gui) do (
    for %%L in (%SEL%) do if /i "%%O"=="%%L" set /a APPCOUNT+=1
)
set "FULLSET=0"
if "%APPCOUNT%"=="5" set "FULLSET=1"

set "CFGS="
if "%DO_RELEASE%"=="1" set "CFGS=!CFGS! release"
if "%DO_DEBUG%"=="1"   set "CFGS=!CFGS! debug"
set "CL="
if "%DO_CLEAN%"=="1" set "CL=-c"

echo.
echo ============================================================
echo  build_exe   cfg=%CFGS% %CL%   sync=%DO_SYNC% verify=%DO_VERIFY%
echo  projects:%SEL%
echo ============================================================

set "FAILED=0"
set "STEPNO=0"
set "FAILED_LIST="

set "SEL_IO=0"
set "SEL_MOTION=0"
set "SEL_SIMBRIDGE=0"
set "SEL_RE=0"
set "SEL_MCTRL=0"
set "SEL_GUI=0"
for %%O in (%SEL%) do (
    if /i "%%O"=="io"        set "SEL_IO=1"
    if /i "%%O"=="motion"    set "SEL_MOTION=1"
    if /i "%%O"=="simbridge" set "SEL_SIMBRIDGE=1"
    if /i "%%O"=="re"        set "SEL_RE=1"
    if /i "%%O"=="mctrl"     set "SEL_MCTRL=1"
    if /i "%%O"=="gui"       set "SEL_GUI=1"
)

if "%DO_SYNC%"=="1" (
    if "%FAILED%"=="0" call :step "sync A9 cmake framework to sub-projects" :sync_a9
) else (
    echo [skip] sync A9  --no-sync
)

if "%SEL_IO%"=="1"     ( call :step "build io (%CFGS% %CL%)" :build_one io )     else ( echo [skip] build io )
if "%FAILED%"=="0" if "%SEL_MOTION%"=="1" ( call :step "build motion (%CFGS% %CL%)" :build_one motion ) else ( if "%FAILED%"=="0" echo [skip] build motion )
if "%FAILED%"=="0" if "%SEL_SIMBRIDGE%"=="1" ( call :step "build simbridge (release)" :build_simbridge ) else ( if "%FAILED%"=="0" echo [skip] build simbridge )
if "%FAILED%"=="0" if "%SEL_RE%"=="1"     ( call :step "build re (%CFGS% %CL%)" :build_one re )     else ( if "%FAILED%"=="0" echo [skip] build re )

if "%DO_SYNC%"=="1" (
    if "%FAILED%"=="0" call :step "sync A1..A5 proto+data to mctrl" :sync_a1a5
) else (
    echo [skip] sync A1..A5  --no-sync
)

if "%FAILED%"=="0" if "%SEL_MCTRL%"=="1" ( call :step "build mctrl (%CFGS% %CL%)" :build_one mctrl ) else ( if "%FAILED%"=="0" echo [skip] build mctrl )

if "%DO_SYNC%"=="1" (
    if "%SEL_GUI%"=="1" if "%FAILED%"=="0" call :step "sync A6..A8 mctrl to config+gui" :sync_a6a8
) else (
    if "%SEL_GUI%"=="1" echo [skip] sync A6..A8  --no-sync
)

if "%FAILED%"=="0" if "%SEL_GUI%"=="1" ( call :step "build gui (%CFGS% %CL%)" :build_one gui ) else ( if "%FAILED%"=="0" echo [skip] build gui )

if "%FAILED%"=="1" goto :build_failed

if "%FULLSET%"=="1" (
    call :step "assemble exe tree" :assemble
) else (
    call :step "deploy built app(s) only" :deploy_selected
)
if "%FAILED%"=="1" goto :build_failed
if "%DO_VERIFY%"=="1" (
    if "%FULLSET%"=="1" (
        call :step "verify exe tree" :verify
    ) else (
        echo [skip] verify  ^(subset deploy - exe\ is not a full package^)
    )
) else (
    echo [skip] verify  --no-verify
)
if "%FAILED%"=="1" goto :build_failed

echo.
echo ============================================================
echo  RESULT : OK   build_exe finished
echo ============================================================
set "_SCRIPT_NO_HOLD=%_HOLD_IN%"
call "%HERE%private_scripts\_common.bat" hold
exit /b 0

:build_failed
echo.
echo ============================================================
echo  RESULT : FAILED - steps:%FAILED_LIST%
echo  exe\ was NOT touched after the last successful assemble.
echo ============================================================
set "_SCRIPT_NO_HOLD=%_HOLD_IN%"
call "%HERE%private_scripts\_common.bat" hold
exit /b 1

rem  Shared exit point of the modes that print no RESULT block of their own
rem  (pack / status / -h): EPIRC holds their exit code.
:epilogue_hold
set "_SCRIPT_NO_HOLD=%_HOLD_IN%"
call "%HERE%private_scripts\_common.bat" hold
exit /b %EPIRC%

rem ---------------- steps ----------------
:build_one
call "%HERE%private_scripts\_build_one.bat" %~1 %CFGS% %CL%
exit /b %ERRORLEVEL%

rem simbridge has no <proj>\scripts\build-*.bat: its one implementation is
rem simulator\simbridge\deploy\rebuild_bin.bat (cmake VS2019 x64 + deploy\bin).
rem Release-only: the relay links module\<m>\output\interface\rpc\lib\release.
rem rebuild_bin.bat fails loudly when deploy\bin is locked (= relay running).
:build_simbridge
if not "%DO_RELEASE%"=="1" goto :simbridge_skip
if "%DO_CLEAN%"=="1" if exist "%ROOT%\simulator\simbridge\relay\build" rmdir /s /q "%ROOT%\simulator\simbridge\relay\build"
call "%ROOT%\simulator\simbridge\deploy\rebuild_bin.bat"
exit /b %ERRORLEVEL%
:simbridge_skip
echo [skip] build simbridge - release only, nothing to do for a debug-only run
exit /b 0

:sync_a9
call "%HERE%private_scripts\_sync_ops.bat" cmake || exit /b 1
exit /b 0

:sync_a1a5
call "%HERE%private_scripts\_sync_ops.bat" proto io || exit /b 1
call "%HERE%private_scripts\_sync_ops.bat" proto motion || exit /b 1
call "%HERE%private_scripts\_sync_ops.bat" proto rm || exit /b 1
call "%HERE%private_scripts\_sync_ops.bat" data re || exit /b 1
exit /b 0

:sync_a6a8
call "%HERE%private_scripts\_sync_ops.bat" data mctrl || exit /b 1
call "%HERE%private_scripts\_sync_ops.bat" proto mctrl || exit /b 1
exit /b 0

:assemble
call "%HERE%private_scripts\_deploy_ops.bat" assemble
exit /b %ERRORLEVEL%

rem  subset deploy: only the selected app folders enter exe\apps\, each one
rem  mirrored from its own output\exe. No config, no platform, no shared.
:deploy_selected
for %%L in (%SEL%) do (
    if /i not "%%L"=="simbridge" (
        echo.
        echo ---- deploy %%L ----
        call "%HERE%private_scripts\_deploy_ops.bat" app %%L
        if errorlevel 1 exit /b 1
    )
)
exit /b 0

:verify
call "%HERE%private_scripts\_deploy_ops.bat" verify
exit /b %ERRORLEVEL%

rem ---------------- step runner ----------------
:step
set /a STEPNO+=1
set "DESC=%~1"
set "LBL=%~2"
set "X1=%~3"
echo.
echo ------------------------------------------------------------
echo  [step %STEPNO%] %DESC%
echo ------------------------------------------------------------
call %LBL% %X1%
rem  step number only in the failure line: DESC may contain brackets
rem  that would close a parenthesised block early (see deploy.bat history)
if not errorlevel 1 exit /b 0
echo [FAILED] step %STEPNO%
set "FAILED=1"
set "FAILED_LIST=!FAILED_LIST! %STEPNO%"
exit /b 1

:usage
echo.
echo Usage: build_exe.bat [release^|debug] [proj...] [-c] [options]
echo        build_exe.bat pack [^<ver^>]
echo        build_exe.bat status
echo.
echo   no arguments : release build of all projects + sync + assemble
echo   release/debug: which configuration to build ^(default release^)
echo   proj...      : io ^| motion ^| simbridge ^| re ^| mctrl ^| gui ^| all
echo                  simbridge is release-only ^(simulation relay^)
echo   -c           : clean each project first
echo   --no-sync    : do not run the cross-project sync steps
echo   --no-verify  : skip the final exe\ verification
echo   pack         : zip the existing exe\ into dist\
echo   status       : exe\ overview + source versions
echo.
echo   Deployment: all five projects ^(default^) = full assemble of exe\ + verify.
echo   A subset ^(e.g. ^"build_exe.bat mctrl^"^) only rebuilds those app folders
echo   under exe\apps\ - config\, platform\, shared\ and the other apps stay
echo   as they are, and verify is skipped.
echo.
echo   This script runs NO git command. Use update_corecode.bat /
echo   update_platform.bat first when you want new code or another
echo   platform version.
echo.
echo Examples:
echo   build_exe.bat
echo   build_exe.bat debug io
echo   build_exe.bat -c mctrl gui --no-sync
echo   build_exe.bat pack
echo.
echo ============================================================
echo  RESULT : OK   usage shown - nothing was built
echo ============================================================
set "EPIRC=0"
goto :epilogue_hold
