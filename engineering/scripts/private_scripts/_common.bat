@echo off
rem ============================================================
rem  Internal helpers for the top-level scripts. NOT a command.
rem
rem  The entry scripts one level up use it like this:
rem      call "%~dp0private_scripts\_common.bat" qt_prefix  || exit /b 1
rem      call "%~dp0private_scripts\_common.bat" summary %RC% "req..." -- "opt..."
rem      call "%~dp0private_scripts\_common.bat" hold
rem
rem  NOTE: `call other.bat :label` does NOT jump to a label - cmd runs the
rem  file from the top and treats ":label" as a plain argument. So the name
rem  is dispatched here explicitly (same trick as the sub-project _lib.bat).
rem
rem  qt_prefix / vs_redist must export variables to the CALLER, so there is
rem  no setlocal around them (only :summary uses one).
rem
rem  ASCII-only on purpose: cmd parses .bat with the console code page
rem  (GBK/936 here), so UTF-8 Chinese comments can swallow the line ending
rem  and corrupt the next line.
rem ============================================================
set "_FN=%~1"
if "%_FN%"=="" (
    echo [ERROR] _common.bat: no helper name given
    exit /b 1
)
if /i "%_FN%"=="qt_prefix" goto :dispatch
if /i "%_FN%"=="vs_redist" goto :dispatch
if /i "%_FN%"=="summary"   goto :dispatch
if /i "%_FN%"=="hold"      goto :dispatch
echo [ERROR] _common.bat: unknown helper "%_FN%"
exit /b 1

rem Same trap as the sub-project _lib.bat: the helper name itself is %1, so it
rem must be shifted away before jumping to the helper's label.
:dispatch
shift
goto :%_FN%

rem ============================================================
rem  Qt prefix. QTDIR wins, otherwise probe the usual install locations.
rem  Must mirror the probe list in istar_common.cmake (shipped inside each
rem  sub-project's cmake\ dir; canonical source engineering\cmake\), because
rem  the platform submodule's preset reads CMAKE_PREFIX_PATH from the
rem  environment and cannot use our CMake-side probe.
rem  Exports: QT_PREFIX
rem ============================================================
:qt_prefix
set "QT_PREFIX="
if defined QTDIR if exist "%QTDIR%\lib\cmake\Qt5" set "QT_PREFIX=%QTDIR%"
if not defined QT_PREFIX if exist "D:\Qt\Qt5.12.12\5.12.12\msvc2017_64\lib\cmake\Qt5" set "QT_PREFIX=D:\Qt\Qt5.12.12\5.12.12\msvc2017_64"
if not defined QT_PREFIX if exist "C:\Qt\Qt5.12.12\5.12.12\msvc2017_64\lib\cmake\Qt5" set "QT_PREFIX=C:\Qt\Qt5.12.12\5.12.12\msvc2017_64"
if not defined QT_PREFIX if exist "D:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" set "QT_PREFIX=D:\Qt\5.15.2\msvc2019_64"
if not defined QT_PREFIX if exist "C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" set "QT_PREFIX=C:\Qt\5.15.2\msvc2019_64"
if not defined QT_PREFIX (
    echo [ERROR] Qt5 not found - set QTDIR to your Qt prefix, e.g.
    echo         setx QTDIR "D:\Qt\Qt5.12.12\5.12.12\msvc2017_64"
    exit /b 1
)
exit /b 0

rem ============================================================
rem  Visual Studio VC142 x64 CRT redist directory.
rem  Needed because exe\ is meant to run on machines without a compiler:
rem  msvcp140 / vcruntime140 / vcruntime140_1 must be shipped.
rem  Exports: VS_REDIST (empty when not found - caller only warns)
rem ============================================================
:vs_redist
set "VS_REDIST="
if defined VCToolsRedistDir if exist "%VCToolsRedistDir%\x64\Microsoft.VC142.CRT\msvcp140.dll" (
    set "VS_REDIST=%VCToolsRedistDir%\x64\Microsoft.VC142.CRT"
    exit /b 0
)
for %%R in (
    "D:\VS2019\2019\Community"
    "D:\VS2019\2019\Professional"
    "D:\Microsoft Visual Studio\2019\Community"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional"
    "C:\Program Files\Microsoft Visual Studio\2019\Community"
    "C:\Program Files\Microsoft Visual Studio\2019\Professional"
) do (
    for /d %%V in ("%%~R\VC\Redist\MSVC\*") do (
        if exist "%%~V\x64\Microsoft.VC142.CRT\msvcp140.dll" set "VS_REDIST=%%~V\x64\Microsoft.VC142.CRT"
    )
)
exit /b 0

rem ============================================================
rem  summary <exitcode> [required path...] [-- optional path...]
rem  (identical format to the sub-project _lib.bat)
rem ============================================================
:summary
setlocal enabledelayedexpansion
set "_RC=%~1"
shift
set "_REQ="
set "_OPT="
set "_SEP="

:sum_collect
if "%~1"=="" goto :sum_print
if "%~1"=="--" (
    set "_SEP=1"
    shift
    goto :sum_collect
)
if defined _SEP (
    set "_OPT=!_OPT! "%~1""
) else (
    set "_REQ=!_REQ! "%~1""
)
shift
goto :sum_collect

:sum_print
set "_MISS=0"
echo.
echo ============================================================
if "%_RC%"=="0" (
    echo  RESULT : OK
) else (
    echo  RESULT : FAILED  ^(exit code %_RC%^)
)
if defined SUMMARY_NOTE echo  %SUMMARY_NOTE%
rem  "tokens=*" strips the leading space the accumulators put in front of
rem  every item, so the line reads the same whichever entry filled the list.
if defined FAILED_LIST for /f "tokens=*" %%F in ("!FAILED_LIST!") do echo  Failed steps: %%F

if not "%_RC%"=="0" (
    echo.
    echo  Look at the [ERROR] lines above to find the failing step,
    echo  fix it, then run this script again.
    goto :sum_tail
)

if defined _REQ (
    echo.
    echo  Produced:
    for %%P in (!_REQ!) do (
        if exist "%%~P" (
            echo    [OK]   %%~P
        ) else (
            echo    [MISS] %%~P
            set "_MISS=1"
        )
    )
)
if defined _OPT (
    echo.
    echo  Optional ^(not produced by this script^):
    for %%P in (!_OPT!) do (
        if exist "%%~P" (
            echo    [have] %%~P
        ) else (
            echo    [none] %%~P
        )
    )
)

:sum_tail
if "!_MISS!"=="1" (
    echo.
    echo  WARNING: a file that should have been produced is missing ^(see [MISS]^).
)
echo ============================================================

call :hold
exit /b 0

rem ============================================================
rem  hold - keep the window open at the end of an ENTRY script.
rem
rem  Called by :summary, and directly by the scripts that print their own
rem  RESULT block (build_exe.bat).
rem
rem  Rules:
rem    - default: hold, so a double-clicked window does not vanish;
rem    - _SCRIPT_NO_HOLD=1 -> do NOT hold. The variable is the CALLER's
rem      wish, so it only ever suppresses the pause of the script being
rem      CALLED: a parent sets it before calling its children and puts its
rem      own incoming value back before its own epilogue (the _HOLD_IN
rem      lines in the entry scripts). A parent must never lose its own
rem      pause by suppressing a child's.
rem    - non-interactive launch -> do NOT hold. Windows starts a
rem      double-clicked .bat as `cmd.exe /c "...bat"`, so a "/c" shows up
rem      in %cmdcmdline%; a script typed into an already open console has
rem      none. This is what keeps automation (which does not set the
rem      variable) from hanging forever on "press any key".
rem ============================================================
:hold
if "%_SCRIPT_NO_HOLD%"=="1" exit /b 0
rem  Do NOT copy %cmdcmdline% into a variable first: the value carries its own
rem  quotes, and pasting it into `set "VAR=<value>"` unbalances them, turning a
rem  "&" inside it into a command separator. Piping it straight into findstr
rem  keeps the original quoting intact.
echo %cmdcmdline%| findstr /i /c:"/c" >nul
if errorlevel 1 exit /b 0
echo.
pause
exit /b 0
