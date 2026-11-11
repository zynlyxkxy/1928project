@echo off
setlocal
rem ============================================================
rem  1928 repository manager - the single entry point.
rem
rem    <this file>              build if sources changed, then run
rem    <this file> -Rebuild     force a rebuild, then run
rem    any other argument       passed straight to the exe
rem                             (e.g. --page 5 --screenshot x.png)
rem
rem  Nothing is rebuilt when the exe is newer than every source file,
rem  so a normal start is an instant launch.
rem
rem  ASCII-only + CRLF on purpose: cmd parses .bat with the console
rem  code page, and goto breaks on LF-only files.
rem ============================================================
set "ROOT=%~dp0"
set "TOOL=%ROOT%release_wizard"
set "EXE=%TOOL%\build\Release\release_wizard.exe"

set "FORCE=0"
set "ARGS="
:parse
if "%~1"=="" goto :parse_done
if /i "%~1"=="-rebuild" (
    set "FORCE=1"
) else (
    set "ARGS=%ARGS% %~1"
)
shift
goto :parse
:parse_done

if "%FORCE%"=="1" goto :build

rem  up to date?  exit 0 = nothing to do, exit 1 = rebuild
set "RM_EXE=%EXE%"
set "RM_SRC=%TOOL%\src"
set "RM_CMAKE=%TOOL%\CMakeLists.txt"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$e = Get-Item -LiteralPath $env:RM_EXE -ErrorAction SilentlyContinue; if ($null -eq $e) { exit 1 }; $n = Get-Item -LiteralPath $env:RM_CMAKE,$env:RM_SRC -ErrorAction SilentlyContinue | ForEach-Object { if ($_.PSIsContainer) { Get-ChildItem -LiteralPath $_.FullName -Recurse -File } else { $_ } } | Where-Object { $_.LastWriteTime -gt $e.LastWriteTime } | Select-Object -First 1; if ($null -eq $n) { exit 0 } else { exit 1 }"
if not errorlevel 1 goto :run

:build
call :build_tool
if errorlevel 1 exit /b 1

:run
call "%TOOL%\run_ui.bat" %ARGS%
exit /b 0

:build_tool
echo.
echo [build] 1928 repository manager - sources newer than the exe
set "_SCRIPT_NO_HOLD=1"
call "%TOOL%\build_ui.bat"
if errorlevel 1 (
    echo.
    echo [ERROR] build failed - see the messages above.
    pause
    exit /b 1
)
exit /b 0
