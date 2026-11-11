@echo off
rem ============================================================
rem  Regenerate io_var_id.h from config\io_config\{acs,plc}\io_config.json
rem
rem  The single generator is module\io\gen_io_var_id.py -- it is what produced
rem  the header currently in the tree. It emits the CmdVarId entries and the
rem  trailing "comment" text, and it refuses to emit a header with duplicate
rem  enum member names (which would be a C2371 on the consumer side).
rem
rem  The embedded-PowerShell version that used to live in this file silently
rem  dropped the whole CmdVarId enum and every comment, so running it broke
rem  mctrl (CMD_PLC_* undefined) -- it is gone. Do not reintroduce a second
rem  generator: keep one source of truth.
rem
rem  Output lands next to this script; copy it where a build needs it by hand
rem  (currently mctrl: MainProject\IOModule\Command\io_var_id.h).
rem
rem  Usage:  gen_io_var_id.bat        (writes .\io_var_id.h)
rem ============================================================
setlocal
set "OUT=%~dp0"
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
set "CFG=%ROOT%\engineering\config\io_config"
set "GEN=%ROOT%\module\io\gen_io_var_id.py"

if not exist "%GEN%" (
    echo [ERROR] generator not found: %GEN%
    exit /b 1
)
if not exist "%CFG%\acs\io_config.json" (
    echo [ERROR] io config not found: %CFG%\acs\io_config.json
    exit /b 1
)

rem python: PATH first, then the interpreter bundled with the 28 model (best effort)
set "PY="
where python >nul 2>nul
if not errorlevel 1 set "PY=python"
if not defined PY (
    where python3 >nul 2>nul
    if not errorlevel 1 set "PY=python3"
)
if not defined PY if exist "%ROOT%\simulator\28module\env\python\python.exe" set "PY=%ROOT%\simulator\28module\env\python\python.exe"
if not defined PY (
    echo [ERROR] python not found on PATH
    exit /b 1
)

rem the generator takes one directory for both input and output, so generate
rem into the config dir and move the header next to this script
"%PY%" "%GEN%" "%CFG%"
if errorlevel 1 (
    echo [ERROR] Failed to generate io_var_id.h
    exit /b 1
)

move /Y "%CFG%\io_var_id.h" "%OUT%io_var_id.h" >nul
if errorlevel 1 (
    echo [ERROR] Generated file found but could not be moved to %OUT%
    exit /b 1
)

echo [OK] Generated: %OUT%io_var_id.h
exit /b 0
