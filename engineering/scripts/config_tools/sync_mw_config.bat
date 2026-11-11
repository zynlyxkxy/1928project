@echo off
rem sync_mw_config.bat
rem Copy engineering\config\mw_config.yaml into the config folder of each subproject
rem (module\io, module\motion, module\re, mctrl, gui) so that a change made
rem in the repo config root is picked up by the subprojects' own build / run
rem folders (they are only used when a subproject is built standalone).
setlocal
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
set "SRC=%ROOT%\engineering\config\mw_config.yaml"

if not exist "%SRC%" (
    echo [ERROR] %SRC% not found
    pause
    exit /b 1
)

echo Sync mw_config.yaml into subproject config folders...
echo.
set "FAILED=0"

for %%t in (
    "module\io\config"
    "module\motion\config"
    "module\re\config"
    "mctrl\config"
    "gui\config"
) do (
    if exist "%ROOT%\%%~t" (
        copy /Y "%SRC%" "%ROOT%\%%~t\" >nul
        if errorlevel 1 (
            echo [FAILED] %%~t  - copy error
            set "FAILED=1"
        ) else (
            echo [OK] mw_config.yaml  -^>  %%~t
        )
    ) else (
        echo [FAILED] %%~t  - folder not found
        set "FAILED=1"
    )
)

echo.
if "%FAILED%"=="1" (
    echo Sync finished with errors - see [FAILED] lines above.
) else (
    echo Sync OK - all 5 subproject configs updated.
)
pause
exit /b %FAILED%
