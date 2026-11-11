@echo off
rem Generic launcher.
rem   %1 = exe path (relative to this folder)
rem   %2 = extra argument for the program, or "-" when it takes none
rem        (motion gets its instance name here: motion1..motion4)
rem   %3 = optional console window title (default: exe name)
rem
rem Search path - this is what makes the package portable:
rem   platform\basiclibrary_output\lib   ISTAR*.dll + Qt5*.dll + platforms + styles
rem                                     (mirror of the platform_output submodule)
rem   shared\qt                          machine-sourced Qt blob: WebEngine modules +
rem                                     imageformats/iconengines/bearer + opengl32sw
rem                                     (never changes unless the Qt install changes)
rem   shared\system                      VC runtime only
rem   platform\rpc_output\libUI*\lib    every libUI* module dll (incl. libUIRm)
rem   platform\exe_output\CTL_Platform\CTL_*\bin\Release
rem                                     module dlls shipped with the external
rem                                     CTL programs (GUI_WM.exe imports ua.dll)
rem   apps\*                            every program folder, because a program may pull a
rem                                     module library that belongs to another program
rem                                     (GUI_WM.exe needs mctrl.dll/mc.dll/rm.dll)
rem
rem Config root (platform contract, 2.0.4+):
rem   PLATFORM_PATH = config\, exported to every launched process. Each process
rem   reads its yaml/json configs and writes its outputs under that root
rem   (<root>\mw_config.yaml, <root>\appconfig.yaml, <root>\<kind>_config\, ...)
rem   instead of writing next to its exe.
rem   The working directory is the same root on purpose: every platform library
rem   whose relative-path base we do not control (Languages\, license_data\, ...)
rem   then lands in config\, never in apps\ or platform\.
rem Each program also carries its own qt.conf (Prefix=../../platform/basiclibrary_output/lib).
if "%~1"=="" echo Usage: run.bat ^<exe relative path^> ^<arg or --^> [window title] & exit /b 1
if "%~2"=="" echo Usage: run.bat ^<exe relative path^> ^<arg or --^> [window title] & exit /b 1

setlocal enabledelayedexpansion
set "ROOT=%~dp0"
set "CONFIG_ROOT=%ROOT%config"
set "PATH=%ROOT%platform\basiclibrary_output\lib;%ROOT%shared\qt;%ROOT%shared\system;%PATH%"
rem NOTE: !PATH! (delayed expansion) is required inside these loops - with %PATH%
rem cmd expands it once at parse time and every iteration overwrites the
rem previous one, leaving only the last directory on PATH.
for /d %%D in ("%ROOT%platform\rpc_output\libUI*") do set "PATH=%%~fD\lib;!PATH!"
rem 2.0.4 moved every external CTL program under exe_output\CTL_Platform\
for /d %%D in ("%ROOT%platform\exe_output\CTL_Platform\CTL_*") do set "PATH=%%~fD\bin\Release;!PATH!"
for /d %%D in ("%ROOT%apps\*") do set "PATH=%%~fD;!PATH!"
rem Qt is built with a hardcoded prefix; point plugin lookup at the runtime layer
rem (two roots: the platform Qt bundle + the machine-sourced plugins in shared\qt)
set "QT_PLUGIN_PATH=%ROOT%platform\basiclibrary_output\lib;%ROOT%shared\qt;%QT_PLUGIN_PATH%"
rem config / output root for every process (platform reads this variable)
set "PLATFORM_PATH=%CONFIG_ROOT%"
cd /d "%CONFIG_ROOT%"
set "EARGS="
if not "%~2"=="-" set "EARGS=%~2"
if "%~3"=="" (
    start "%~n1" "%ROOT%%~1" %EARGS%
) else (
    start "%~3" "%ROOT%%~1" %EARGS%
)
endlocal
exit /b 0
