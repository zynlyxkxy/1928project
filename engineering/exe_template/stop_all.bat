@echo off
rem Close every exe that start_all.bat may have launched (plus Qt helper).
echo [stop_all] Closing control / module / gui processes...
for %%p in (
    CTL_SE_EH.exe
    CTL_SE_ST.exe
    CTL_SE_TR.exe
    CTL_SE_UA.exe
    CTL_TL_RP.exe
    CTL_TL_MC.exe
    CTL_TL_PS.exe
    CTL_TL_MT.exe
    CTL_TL_RM.exe
    CTL_MDL_IO.exe
    CTL_MDL_Motion.exe
    MCtrl_WM.exe
    GUI_WM.exe
    GUI_MDL_IO.exe
    GUI_MDL_Motion.exe
    GUI_TL_RM_Wafer.exe
    GUI_TL_RM_CHIP.exe
    QtWebEngineProcess.exe
) do (
    taskkill /F /IM %%p >nul 2>nul
    if not errorlevel 1 (
        echo   killed %%p
    ) else (
        rem taskkill also fails when the process simply is not running;
        rem only warn when it IS still listed - that is a kernel-stuck
        rem zombie that no taskkill can remove, closing its console
        rem window or a reboot is the only way out
        tasklist /FI "IMAGENAME eq %%p" 2>nul | findstr /i /c:"%%p" >nul
        if not errorlevel 1 (
            echo   [WARN] %%p survived taskkill - kernel-stuck, close its console window or reboot
        )
    )
)
rem --- simulation cleanup: relay layer + 28model backend
rem     (started by start_all.bat -s / simulation mode) ---
call "%~dp0..\simulator\simbridge\deploy\stop_relay.bat"
echo [stop_all] stopping 28model backend ...
powershell -NoProfile -Command "$p = Get-CimInstance Win32_Process | Where-Object { $_.Name -match '^python' -and $_.CommandLine -match 'runtime[/\\]main\.py' }; if ($p) { $p | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue; Write-Host ('  stopped 28model backend pid ' + $_.ProcessId) } } else { Write-Host '  no 28model backend running' }"
echo [stop_all] Done.
exit /b 0
