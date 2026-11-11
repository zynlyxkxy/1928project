@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  fetch_if_stale.bat <repo-path>
rem
rem  One SSH round trip per repo:
rem    - prints every ref (sha TAB ref) so the UI can reuse the tag list
rem    - fetches only when tags are missing locally or develop has new work
rem
rem  Used by the UI refresh worker (main repo + 5 sub-repos).
rem  ASCII-only + CRLF on purpose.
rem ============================================================
set "RP=%~1"
if "%RP%"=="" exit /b 1

set "REMOTE="
set /a RTAGS=0
for /f "tokens=1,2" %%A in ('git -C "%RP%" ls-remote origin 2^>nul') do (
    if /i "%%B"=="refs/heads/develop" set "REMOTE=%%A"
    echo %%A	%%B
    echo %%B | findstr /c:"^{}" >nul
    if errorlevel 1 (
        echo %%B | findstr /b /c:"refs/tags/" >nul
        if not errorlevel 1 set /a RTAGS+=1
    )
)
if not defined REMOTE exit /b 1

set /a LTAGS=0
for /f %%T in ('git -C "%RP%" tag --list 2^>nul') do set /a LTAGS+=1
if !LTAGS! LSS !RTAGS! goto :fetch

set "LOCAL="
for /f "tokens=1" %%S in ('git -C "%RP%" rev-parse develop 2^>nul') do set "LOCAL=%%S"
if not defined LOCAL goto :fetch

if /i "%REMOTE%"=="%LOCAL%" exit /b 0

rem Remote commit known locally and already contained in develop -> nothing new.
git -C "%RP%" cat-file -e "%REMOTE%^{commit}" >nul 2>nul || goto :fetch
git -C "%RP%" merge-base --is-ancestor "%REMOTE%" develop >nul 2>nul
if not errorlevel 1 exit /b 0

:fetch
git -C "%RP%" fetch --tags origin
exit /b %ERRORLEVEL%
