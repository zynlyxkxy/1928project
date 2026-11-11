@echo off
rem ============================================================
rem  tag_version.bat - build the release map.
rem
rem    tag_version.bat                    regenerate CHANGELOG.md / release-map.html
rem    tag_version.bat -DryRun            build and report, write nothing
rem    tag_version.bat -h                 this help
rem
rem  Project settings (tag prefix, release doc, generated file paths,
rem  sub-repo list and their tag prefixes) live in the param block at
rem  the top of private_scripts\_tag_version_main.ps1.
rem
rem  Nothing is tagged, committed or pushed here: the tags are made by
rem  hand. One section of engineering\version\RELEASE.md is one main
rem  tag, and its heading carries the tag name without the prefix:
rem
rem      ## <main> 2.1.0_Rel_2026113001
rem
rem  ASCII-only on purpose. NOTE: the ps1 is UTF-8 WITH BOM because
rem  it contains the Chinese headings used in the generated notes.
rem ============================================================
setlocal EnableExtensions
set "HERE=%~dp0"

rem ---- PS1 location, relative to THIS .bat. Change this one line if yours differs.
set "PS1=private_scripts\_tag_version_main.ps1"
set "PS1=%HERE%%PS1%"
if not exist "%PS1%" (
    echo [ERROR] _tag_version_main.ps1 not found
    echo         look for: %PS1%
    exit /b 1
)

rem ---- optional shared summary helper: used when present, skipped when not.
set "LIB=private_scripts\_common.bat"
set "LIB=%HERE%%LIB%"
if not exist "%LIB%" set "LIB="

if /i "%~1"=="-h" goto :help
if /i "%~1"=="/?" goto :help
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" %*
set "RC=%ERRORLEVEL%"
call :summary %RC%
exit /b %RC%

:help
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -Help
set "RC=%ERRORLEVEL%"
call :summary %RC%
exit /b %RC%

rem ============================================================
rem  :summary - use the project helper when it exists, otherwise
rem  print the same block here. Holds the window only on a
rem  double-click.
rem ============================================================
:summary
if defined LIB (
    call "%LIB%" summary %~1
    goto :eof
)
echo.
echo ============================================================
if "%~1"=="0" (
    echo  RESULT : OK
) else (
    echo  RESULT : FAILED  ^(exit code %~1^)
)
echo ============================================================
if "%_SCRIPT_NO_HOLD%"=="1" goto :eof
echo %cmdcmdline%| findstr /i /c:"/c" >nul
if errorlevel 1 goto :eof
pause
goto :eof
