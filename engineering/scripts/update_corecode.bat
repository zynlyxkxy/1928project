@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  update_corecode.bat - update the CODE repositories only.
rem
rem    update_corecode.bat                     all code repos -> develop + pull
rem    update_corecode.bat io gui              only these repos
rem    update_corecode.bat --except motion re  all except these
rem    update_corecode.bat -h                  this help
rem
rem  Code repos: io motion re mctrl gui.
rem  NO submodule is touched here. Platform mounts belong to
rem  engineering\scripts\update_platform.bat; third-party repos (simulator and
rem  mctrl\third_party) belong to engineering\scripts\update_thirdparty.bat - which is
rem  also why simulator is no longer one of the "code repos" here.
rem
rem  ASCII-only on purpose.
rem ============================================================
set "HERE=%~dp0"
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
rem  Hold policy: _SCRIPT_NO_HOLD=1 tells the scripts CALLED below not to
rem  stop; our own epilogue must still stop, so the caller's value is saved
rem  here and put back before we print the RESULT block (see :epilogue and
rem  _common.bat :hold).
set "_HOLD_IN=%_SCRIPT_NO_HOLD%"
set "_SCRIPT_NO_HOLD=1"

set "ALL=io motion re mctrl gui"
set "SEL="
set "EXCEPT="
set "EXPECT_EXCEPT=0"
set "FAILED=0"

:parse
if "%~1"=="" goto :parse_done
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage
if /i "%~1"=="--except" (
    set "EXPECT_EXCEPT=1"
) else if "!EXPECT_EXCEPT!"=="1" (
    set "EXCEPT=!EXCEPT! %~1"
) else (
    set "SEL=!SEL! %~1"
)
shift
goto :parse
:parse_done

where git >nul 2>nul
if errorlevel 1 (
    echo [SKIP] no git found - nothing to update
    set "RC=0"
    goto :epilogue
)

rem validate every requested name
for %%N in (%SEL% %EXCEPT%) do call :is_known "%%~N" || (
    echo [ERROR] update_corecode.bat: unknown repo "%%~N" - known: %ALL%
    set "RC=1"
    goto :epilogue
)

if not "%SEL%%EXCEPT%"=="" goto :have_selection

rem  Fail fast: the first repo that fails stops the remaining ones, so a
rem  failed run never leaves half the repos updated behind your back.
for %%P in (%ALL%) do (
    call :pull_one "%%P"
    if errorlevel 1 (
        set "FAILED_LIST=!FAILED_LIST! %%P"
        goto :report
    )
)
goto :report

:have_selection
if "%EXCEPT%"=="" goto :only_sel
rem --except: run the complement of EXCEPT within ALL
for %%P in (%ALL%) do (
    set "SKIPIT=0"
    for %%E in (%EXCEPT%) do if /i "%%P"=="%%E" set "SKIPIT=1"
    if "!SKIPIT!"=="0" (
        call :pull_one "%%P"
        if errorlevel 1 (
            set "FAILED_LIST=!FAILED_LIST! %%P"
            goto :report
        )
    )
)
goto :report

:only_sel
for %%P in (%SEL%) do (
    call :pull_one "%%P"
    if errorlevel 1 (
        set "FAILED_LIST=!FAILED_LIST! %%P"
        goto :report
    )
)

:report
if "%FAILED%"=="1" (
    echo [ERROR] update_corecode: at least one repo failed - see the [ERROR] lines above
    set "RC=1"
) else (
    echo [OK] update_corecode done
    set "RC=0"
)
goto :epilogue

:is_known
set "K="
for %%K in (%ALL%) do if /i "%~1"=="%%K" set "K=1"
if not defined K exit /b 1
exit /b 0

:pull_one
set "P=%~1"
set "RELP="
if /i "%P%"=="io"       set "RELP=module\io"
if /i "%P%"=="motion"   set "RELP=module\motion"
if /i "%P%"=="re"       set "RELP=module\re"
if /i "%P%"=="mctrl"    set "RELP=mctrl"
if /i "%P%"=="gui"      set "RELP=gui"
echo.
echo ---- %P% : checkout develop + pull ----
if not exist "%ROOT%\%RELP%\.git" (
    echo [INFO] submodule not initialized, running git submodule update --init %RELP%
    pushd "%ROOT%"
    git submodule update --init "%RELP%"
    if errorlevel 1 ( popd & echo [ERROR] init submodule %P% failed & set "FAILED=1" & exit /b 1 )
    popd
)
pushd "%ROOT%\%RELP%"
if not exist ".git" (
    popd
    echo [SKIP] %P% is not a git repo - skipped
    exit /b 0
)
git checkout develop 2>nul
if errorlevel 1 (
    git checkout -b develop origin/develop
    if errorlevel 1 ( popd & echo [ERROR] %P%: checkout develop failed & set "FAILED=1" & exit /b 1 )
)
rem 本地 develop 可能是手动建的、没有 upstream，git pull 会直接失败
git rev-parse --abbrev-ref --symbolic-full-name "@{u}" >nul 2>nul
if errorlevel 1 (
    git branch --set-upstream-to=origin/develop develop >nul
    if errorlevel 1 ( popd & echo [ERROR] %P%: cannot set upstream to origin/develop & set "FAILED=1" & exit /b 1 )
)
git pull
if errorlevel 1 ( popd & echo [ERROR] %P%: git pull failed - network/auth/conflict? & set "FAILED=1" & exit /b 1 )
popd
echo [OK] %P%
exit /b 0

:usage
echo.
echo Usage: update_corecode.bat [--except ^<repo...^>] [^<repo...^>] [-h]
echo.
echo   Repos: io motion re mctrl gui
echo   no arguments : update all of them ^(checkout develop + pull^)
echo   io gui        : update only these
echo   --except m re : update all except these
echo.
echo   This script never touches submodules - use update_platform.bat
echo   for platform versions and update_thirdparty.bat for third-party
echo   repos ^(simulator, mctrl\third_party^).
echo.
echo Examples:
echo   update_corecode.bat
echo   update_corecode.bat io motion
echo   update_corecode.bat --except mctrl
echo.
set "RC=0"
goto :epilogue

rem ============================================================
rem  Single exit point: put the caller's hold wish back (the "1" we set to
rem  silence the child scripts must not silence our own pause), print the
rem  RESULT block, then hold. See _common.bat :hold.
rem ============================================================
:epilogue
set "_SCRIPT_NO_HOLD=%_HOLD_IN%"
call "%HERE%private_scripts\_common.bat" summary %RC%
exit /b %RC%
