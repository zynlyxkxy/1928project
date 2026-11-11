@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  start_all.bat - launch the machine programs.
rem
rem    start_all.bat              start the default set, real io/motion
rem    start_all.bat -s           simulation mode: io/motion controllers
rem                                replaced by simbridge relay + 28model
rem    start_all.bat 1 5 gui      start only these - number or name,
rem                                see the table with -h
rem    start_all.bat -s 12        flags and selection combine
rem    start_all.bat -h           show everything startable
rem
rem  Every launch goes through run.bat: it sets the dll search PATH, exports
rem  PLATFORM_PATH=<package>\config as the single config/output root and makes
rem  that root the working directory. A program that runs in several instances
rem  (motion1..motion4) gets its instance name as the only argument; the process
rem  reads <root>\motion_config\<instance>\config.json itself.
rem  Not startable on purpose: sm tr ps st - the platform ships those exes but
rem  this package wires no config for them (no start entry, no check in :resolve).
rem
rem  ASCII-only on purpose.
rem ============================================================
set "ROOT=%~dp0"
set "SIM=0"
set "SEL="

:parse
if "%~1"=="" goto :parse_done
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage
if /i "%~1"=="-s" (
    set "SIM=1"
) else (
    set "SEL=!SEL! %~1"
)
shift
goto :parse
:parse_done

if "%SEL%"=="" goto :default_set

set "FAILED=0"
for %%K in (%SEL%) do (
    call :go %%K
    if errorlevel 1 set "FAILED=1"
)
if "%FAILED%"=="1" (
    echo [start_all] selected set had errors - see the [ERROR] lines above.
    exit /b 1
)
echo [start_all] selected set started.
exit /b 0

:default_set
call :go eh
call :go mc
call :go rp
call :go ua
call :go mt
call :go re
if "%SIM%"=="1" (
    echo [start_all] SIMULATION mode: io/motion controllers replaced by relay + 28model
    start "28model-sim" cmd /c "%ROOT%..\simulator\28module\start.bat"
    rem 28model backend port 9010 must be up before relay and MCtrl/GUI start
    call :wait_port 9010 60
    call "%ROOT%..\simulator\simbridge\start_simbridge.bat"
) else (
    call :go io
    call :go motion1
    call :go motion2
    call :go motion3
    call :go motion4
)
call :go mctrl
call :go gui
echo [start_all] default set started ^(simulation=%SIM%^).
exit /b 0

rem ------------------------------------------------------------
rem  resolve number-or-name, validate, hand over to run.bat
rem ------------------------------------------------------------
:go
set "KEY=%~1"
set "EXE="
set "ARG=-"
set "TIT="
call :resolve
if not defined EXE (
    echo [ERROR] start_all.bat: unknown program "%KEY%" - try -h
    exit /b 1
)
if not exist "%ROOT%%EXE%" (
    echo [ERROR] start_all.bat: missing exe: %EXE%
    exit /b 1
)
if "%SIM%"=="1" call :sim_guard
if not defined EXE exit /b 0
echo [start] %KEY% : %EXE%  (arg=%ARG%)
call "%ROOT%run.bat" %EXE% %ARG% "%TIT%"
exit /b 0

:sim_guard
rem in simulation mode the real io/motion controllers are replaced by the relay
for %%S in (io motion1 motion2 motion3 motion4 7 8 9 10 11) do if /i "%KEY%"=="%%S" (
    echo [skip] %KEY% in simulation mode - relay + 28model take over
    set "EXE="
)
exit /b 0

:resolve
rem one line per alias: number and name resolve to the same entry
rem   EXE  = exe path under this folder
rem   ARG  = extra argument for the program ("-" = none); motion gets its
rem          instance name (motion1..motion4)
rem Platform-supplied programs live under platform\exe_output\CTL_Platform\ since 2.0.4
if /i "%KEY%"=="1"  (set "EXE=platform\exe_output\CTL_Platform\CTL_SE_EH\bin\Release\CTL_SE_EH.exe" & set "ARG=-" & set "TIT=EH Control")
if /i "%KEY%"=="eh" (set "EXE=platform\exe_output\CTL_Platform\CTL_SE_EH\bin\Release\CTL_SE_EH.exe" & set "ARG=-" & set "TIT=EH Control")
if /i "%KEY%"=="2"  (set "EXE=platform\exe_output\CTL_Platform\CTL_TL_MC\bin\Release\CTL_TL_MC.exe" & set "ARG=-" & set "TIT=MC Control")
if /i "%KEY%"=="mc" (set "EXE=platform\exe_output\CTL_Platform\CTL_TL_MC\bin\Release\CTL_TL_MC.exe" & set "ARG=-" & set "TIT=MC Control")
if /i "%KEY%"=="3"  (set "EXE=platform\exe_output\CTL_Platform\CTL_TL_RP\bin\Release\CTL_TL_RP.exe" & set "ARG=-" & set "TIT=RP Control")
if /i "%KEY%"=="rp" (set "EXE=platform\exe_output\CTL_Platform\CTL_TL_RP\bin\Release\CTL_TL_RP.exe" & set "ARG=-" & set "TIT=RP Control")
if /i "%KEY%"=="4"  (set "EXE=platform\exe_output\CTL_Platform\CTL_SE_UA\bin\Release\CTL_SE_UA.exe" & set "ARG=-" & set "TIT=UA Control")
if /i "%KEY%"=="ua" (set "EXE=platform\exe_output\CTL_Platform\CTL_SE_UA\bin\Release\CTL_SE_UA.exe" & set "ARG=-" & set "TIT=UA Control")
if /i "%KEY%"=="5"  (set "EXE=platform\exe_output\CTL_Platform\CTL_TL_MT\bin\Release\CTL_TL_MT.exe" & set "ARG=-" & set "TIT=MT Control")
if /i "%KEY%"=="mt" (set "EXE=platform\exe_output\CTL_Platform\CTL_TL_MT\bin\Release\CTL_TL_MT.exe" & set "ARG=-" & set "TIT=MT Control")
if /i "%KEY%"=="6"  (set "EXE=apps\re\CTL_TL_RM.exe" & set "ARG=-" & set "TIT=RM Control")
if /i "%KEY%"=="re" (set "EXE=apps\re\CTL_TL_RM.exe" & set "ARG=-" & set "TIT=RM Control")
if /i "%KEY%"=="7"  (set "EXE=apps\io\CTL_MDL_IO.exe" & set "ARG=-" & set "TIT=IO Control")
if /i "%KEY%"=="io" (set "EXE=apps\io\CTL_MDL_IO.exe" & set "ARG=-" & set "TIT=IO Control")
if /i "%KEY%"=="8"  (set "EXE=apps\motion\CTL_MDL_Motion.exe" & set "ARG=motion1" & set "TIT=Motion Control - motion1")
if /i "%KEY%"=="motion1" (set "EXE=apps\motion\CTL_MDL_Motion.exe" & set "ARG=motion1" & set "TIT=Motion Control - motion1")
if /i "%KEY%"=="9"  (set "EXE=apps\motion\CTL_MDL_Motion.exe" & set "ARG=motion2" & set "TIT=Motion Control - motion2")
if /i "%KEY%"=="motion2" (set "EXE=apps\motion\CTL_MDL_Motion.exe" & set "ARG=motion2" & set "TIT=Motion Control - motion2")
if /i "%KEY%"=="10" (set "EXE=apps\motion\CTL_MDL_Motion.exe" & set "ARG=motion3" & set "TIT=Motion Control - motion3")
if /i "%KEY%"=="motion3" (set "EXE=apps\motion\CTL_MDL_Motion.exe" & set "ARG=motion3" & set "TIT=Motion Control - motion3")
if /i "%KEY%"=="11" (set "EXE=apps\motion\CTL_MDL_Motion.exe" & set "ARG=motion4" & set "TIT=Motion Control - motion4")
if /i "%KEY%"=="motion4" (set "EXE=apps\motion\CTL_MDL_Motion.exe" & set "ARG=motion4" & set "TIT=Motion Control - motion4")
if /i "%KEY%"=="12" (set "EXE=apps\mctrl\MCtrl_WM.exe" & set "ARG=-" & set "TIT=MCtrl_WM")
if /i "%KEY%"=="mctrl" (set "EXE=apps\mctrl\MCtrl_WM.exe" & set "ARG=-" & set "TIT=MCtrl_WM")
if /i "%KEY%"=="13" (set "EXE=apps\gui\GUI_WM.exe" & set "ARG=-" & set "TIT=GUI_WM")
if /i "%KEY%"=="gui" (set "EXE=apps\gui\GUI_WM.exe" & set "ARG=-" & set "TIT=GUI_WM")
exit /b 0

rem wait until a loopback port is LISTENING (poll every 2s)
rem %1 = port, %2 = timeout seconds; on timeout prints a warning and
rem returns errorlevel 1 (caller decides whether to continue)
:wait_port
set /a _t=0
:wait_port_loop
netstat -ano | findstr "127.0.0.1:%~1" | findstr LISTENING >nul 2>nul
if not errorlevel 1 (
    echo   [start_all] port %~1 is up
    exit /b 0
)
set /a _t+=2
if %_t% GEQ %~2 (
    echo   [start_all] WARN port %~1 not listening within %~2s - starting anyway
    exit /b 1
)
timeout /t 2 /nobreak >nul
goto :wait_port_loop

:usage
echo.
echo Usage: start_all.bat [-s] [number or name ...]
echo.
echo   -s      simulation mode: io/motion controllers are replaced by
echo           simbridge relay + 28model simulator ^(they are skipped^)
echo   no selection = the default set: eh mc rp ua mt re io motion1-4 mctrl gui
echo.
echo   #   name      exe                                                config dir
echo  ---------------------------------------------------------------------------
echo   1   eh        platform\exe_output\CTL_Platform\CTL_SE_EH\...      eh_config
echo   2   mc        platform\exe_output\CTL_Platform\CTL_TL_MC\...      mc_config
echo   3   rp        platform\exe_output\CTL_Platform\CTL_TL_RP\...      rp_config
echo   4   ua        platform\exe_output\CTL_Platform\CTL_SE_UA\...      ua_config
echo   5   mt        platform\exe_output\CTL_Platform\CTL_TL_MT\...      appconfig.yaml
echo   6   re        apps\re\CTL_TL_RM.exe                               rm_config
echo   7   io        apps\io\CTL_MDL_IO.exe                              io_config
echo   8   motion1   apps\motion\CTL_MDL_Motion.exe motion1             motion_config\motion1
echo   9   motion2   apps\motion\CTL_MDL_Motion.exe motion2             motion_config\motion2
echo  10   motion3   apps\motion\CTL_MDL_Motion.exe motion3             motion_config\motion3
echo  11   motion4   apps\motion\CTL_MDL_Motion.exe motion4             motion_config\motion4
echo  12   mctrl     apps\mctrl\MCtrl_WM.exe                             mctrl_config
echo  13   gui       apps\gui\GUI_WM.exe                                 gui_config
echo.
echo   Config root:  exe\config\  exported as PLATFORM_PATH (also the cwd).
echo   Not startable on purpose: sm tr ps st ^(exe shipped, no config wired^).
echo.
echo Examples:
echo   start_all.bat
echo   start_all.bat 1 6
echo   start_all.bat mc gui
echo   start_all.bat -s mctrl gui
echo.
exit /b 0
