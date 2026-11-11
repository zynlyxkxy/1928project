@echo off
setlocal
rem ============================================================
rem  run_ui.bat - launch the 1928 repository manager.
rem  Working directory is the repo root, so the app can find .git.
rem ============================================================
set "HERE=%~dp0"
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "EXE=%ROOT%\release_wizard\build\Release\release_wizard.exe"

if not exist "%EXE%" (
    echo [ERROR] release_wizard.exe not found.
    echo         Run release_wizard\build_ui.bat first.
    if not "%_SCRIPT_NO_HOLD%"=="1" pause
    exit /b 1
)

cd /d "%ROOT%"
start "" "%EXE%" %*
exit /b 0
