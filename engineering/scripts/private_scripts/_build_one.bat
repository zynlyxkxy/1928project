@echo off
rem ============================================================
rem  Build one sub-project (or all of them) through its own scripts.
rem
rem    build_one.bat <proj> [release] [debug] [-c|clean]
rem    build_one.bat all   [release] [debug] [-c|clean]
rem
rem    <proj> : io | motion | re | mctrl | gui
rem             (module\io / module\motion / module\re also accepted)
rem
rem  There is exactly one implementation per project: <proj>\scripts\
rem  build-release.bat / build-debug.bat / clean.bat. Those do the real work
rem  (configure + build + install into output\, and for re also the platform
rem  submodule build). This script only picks the right config and checks the
rem  return code, so a build driven from here and a build driven from inside
rem  the project produce exactly the same thing.
rem
rem  Default when neither release nor debug is given: release.
rem
rem  ASCII-only on purpose: see the note in engineering\scripts\private_scripts\_common.bat.
rem ============================================================
setlocal enabledelayedexpansion
set "HERE=%~dp0"
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
rem  Hold policy: _SCRIPT_NO_HOLD=1 tells the project build scripts CALLED
rem  below not to stop; our own summary must still stop, so the caller's
rem  value is saved here and put back before the summary (see :build_done).
set "_HOLD_IN=%_SCRIPT_NO_HOLD%"
set "_SCRIPT_NO_HOLD=1"

set "PROJ=%~1"
if "%PROJ%"=="" (
    echo [ERROR] _build_one.bat: missing project. Known: io motion re mctrl gui all
    exit /b 1
)
shift

set "DO_RELEASE=0"
set "DO_DEBUG=0"
set "DO_CLEAN=0"
:parse
if "%~1"=="" goto :parse_done
if /i "%~1"=="release" set "DO_RELEASE=1"
if /i "%~1"=="debug"   set "DO_DEBUG=1"
if /i "%~1"=="-c"      set "DO_CLEAN=1"
if /i "%~1"=="clean"   set "DO_CLEAN=1"
if /i not "%~1"=="release" if /i not "%~1"=="debug" if /i not "%~1"=="-c" if /i not "%~1"=="clean" (
    echo [ERROR] _build_one.bat: unknown argument "%~1"
    exit /b 1
)
shift
goto :parse
:parse_done
if "%DO_RELEASE%%DO_DEBUG%"=="00" set "DO_RELEASE=1"

set "BLDRC=0"
if /i "%PROJ%"=="all" (
    for %%P in (module\io module\motion module\re mctrl gui) do (
        call :build_proj "%%P" || goto :build_failed
    )
) else (
    call :proj_path "%PROJ%" || exit /b 1
    call :build_proj "!PROJ_PATH!" || goto :build_failed
)
goto :build_done

rem  Fail fast: the first project that fails aborts the rest of the chain.
rem  FAILED_LIST (filled in by :build_proj) names the failing step.
:build_failed
set "BLDRC=1"

:build_done
set "SUMMARY_NOTE=build_one %PROJ% (release=%DO_RELEASE% debug=%DO_DEBUG% clean=%DO_CLEAN%)"
set "_SCRIPT_NO_HOLD=%_HOLD_IN%"
call "%HERE%_common.bat" summary %BLDRC%
exit /b %BLDRC%

rem ============================================================
rem  Projects are built in dependency order by the caller; this label only
rem  handles one project.
rem ============================================================
:build_proj
set "PPATH=%~1"
set "PN=%PPATH%"
if /i "%PN%"=="module\io"     set "PN=io"
if /i "%PN%"=="module\motion" set "PN=motion"
if /i "%PN%"=="module\re"     set "PN=re"

if not exist "%ROOT%\%PPATH%\scripts\build-release.bat" (
    echo [ERROR] %PPATH%\scripts\build-release.bat not found - project not migrated yet?
    exit /b 1
)

if "%DO_CLEAN%"=="1" (
    echo.
    echo ===== clean %PN% =====
    call "%ROOT%\%PPATH%\scripts\clean.bat" || (
        echo [ERROR] clean failed for %PN%
        set "FAILED_LIST=%PN% clean"
        exit /b 1
    )
)

if "%DO_RELEASE%"=="1" (
    echo.
    echo ===== build %PN% release =====
    call "%ROOT%\%PPATH%\scripts\build-release.bat" || (
        echo [ERROR] release build failed for %PN%
        set "FAILED_LIST=%PN% release"
        exit /b 1
    )
)
if "%DO_DEBUG%"=="1" (
    echo.
    echo ===== build %PN% debug =====
    call "%ROOT%\%PPATH%\scripts\build-debug.bat" || (
        echo [ERROR] debug build failed for %PN%
        set "FAILED_LIST=%PN% debug"
        exit /b 1
    )
)
exit /b 0

rem ============================================================
rem  Resolve a project token to its path relative to the project root.
rem  Accepts both the short name and a path.
rem ============================================================
:proj_path
set "PROJ_PATH="
if /i "%~1"=="io"     set "PROJ_PATH=module\io"
if /i "%~1"=="motion" set "PROJ_PATH=module\motion"
if /i "%~1"=="re"     set "PROJ_PATH=module\re"
if /i "%~1"=="mctrl"  set "PROJ_PATH=mctrl"
if /i "%~1"=="gui"    set "PROJ_PATH=gui"
if /i "%~1"=="module\io"     set "PROJ_PATH=module\io"
if /i "%~1"=="module\motion" set "PROJ_PATH=module\motion"
if /i "%~1"=="module\re"     set "PROJ_PATH=module\re"
if not defined PROJ_PATH (
    echo [ERROR] unknown project "%~1". Known: io motion re mctrl gui
    exit /b 1
)
exit /b 0
