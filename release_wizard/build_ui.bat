@echo off
setlocal
rem ============================================================
rem  build_ui.bat - build the 1928 repository manager (Qt).
rem  Source: this folder. Output stays out of git.
rem
rem  Qt is located by CMakeLists.txt (QTDIR first, then the usual
rem  install locations), so this script needs no Qt setup at all.
rem  The Qt runtime is deployed by a POST_BUILD step (windeployqt).
rem
rem  The generator is picked from the Visual Studio actually installed
rem  (vswhere, newest first); nothing is pinned to one version.
rem
rem  ASCII-only + CRLF on purpose: cmd parses .bat with the console
rem  code page, and goto breaks on LF-only files.
rem ============================================================
set "HERE=%~dp0"
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
for %%I in ("%~dp0.") do set "TOOL=%%~fI"

rem ---- pick the newest installed Visual Studio ----
set "GEN="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "tokens=1 delims=." %%V in ('"%VSWHERE%" -latest -products * -property installationVersion 2^>nul') do (
        if "%%V"=="17" set "GEN=Visual Studio 17 2022"
        if "%%V"=="16" set "GEN=Visual Studio 16 2019"
        if "%%V"=="15" set "GEN=Visual Studio 15 2017"
    )
)
if not defined GEN set "GEN=Visual Studio 16 2019"

echo.
echo ============================================================
echo  build release_wizard
echo  tool : %TOOL%
echo  gen  : %GEN%
echo ============================================================
echo.

cmake -S "%TOOL%" -B "%TOOL%\build" -G "%GEN%" -A x64
if errorlevel 1 goto :failed

cmake --build "%TOOL%\build" --config Release
if errorlevel 1 goto :failed

echo.
echo [OK] build_ui done: %TOOL%\build\Release\release_wizard.exe
if not "%_SCRIPT_NO_HOLD%"=="1" pause
exit /b 0

:failed
echo.
echo [ERROR] build_ui failed.
if not "%_SCRIPT_NO_HOLD%"=="1" pause
exit /b 1
