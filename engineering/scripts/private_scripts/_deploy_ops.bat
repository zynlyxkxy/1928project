@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  Assembly engine: exe\ is a pure copy-combination of four sources.
rem
rem    _deploy_ops.bat init-exe    recreate exe\ skeleton from exe_template
rem                                (exe\config is PRESERVED - field config)
rem    _deploy_ops.bat apps        <proj>\output\exe -> exe\apps\<name>   (MIRROR)
rem    _deploy_ops.bat app <name>  only ONE app (io|motion|re|mctrl|gui):
rem                                mirror it into exe\apps\<name>, touch
rem                                nothing else - no config, no platform,
rem                                no other app
rem    _deploy_ops.bat platform    platform_output -> exe\platform        (MIRROR)
rem                                excluding CTL_TL_RM Platform Resource
rem                                Config tool bats
rem    _deploy_ops.bat shared      engineering\runtime\ -> exe\shared      (MIRROR)
rem    _deploy_ops.bat config      engineering\config\ -> exe\config (A6, legacy dirs
rem                                migrated, field data kept)
rem    _deploy_ops.bat versions    write exe\VERSIONS.txt
rem    _deploy_ops.bat assemble    all of the above, in order
rem    _deploy_ops.bat verify      file-level checks of the assembled tree
rem    _deploy_ops.bat status      per-app file counts + platform tags
rem    _deploy_ops.bat pack [ver]  zip exe\ -> dist\1927-exe-<ver>.zip
rem
rem  exe\ layout (each folder == exactly one source, wholesale replace):
rem    exe\apps\<io|motion|re|mctrl|gui>\  <- each project's output\exe
rem    exe\platform\basiclibrary_output\lib\   \rpc_output\   \exe_output\
rem                                       <- platform_output submodule
rem                                       (2.0.4+: platform exes under
rem                                        exe_output\CTL_Platform\CTL_*)
rem    exe\shared\qt   exe\shared\system   <- engineering\runtime\ (in-repo, Qt/VC blob)
rem    exe\config\                          <- engineering\config\ (single config root,
rem                                            exported as PLATFORM_PATH)
rem
rem  Every copy is a MIRROR: files that disappeared from the source are
rem  removed here, so replacing a folder is always safe.
rem  robocopy gets /R:1 /W:1: a locked file must fail fast instead of
rem  retrying for hours (stop_all.bat first).
rem
rem  ASCII-only on purpose.
rem ============================================================
set "HERE=%~dp0"
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
rem  Never stop here, and never let anything we call stop either: this file
rem  is only ever driven by an entry script (build_exe.bat). See the hold
rem  rules in _common.bat.
set "_SCRIPT_NO_HOLD=1"
set "EXE=%ROOT%\exe"
set "TPL=%ROOT%\engineering\exe_template"
set "PLATOUT=%ROOT%\platform_output"
set "RUNTIME=%ROOT%\engineering\runtime"
set "RCOPT=/NFL /NDL /NJH /NJS /NP /R:1 /W:1"
set "FAILED=0"

set "CMD=%~1"
if "%CMD%"=="" set "CMD=assemble"

if not exist "%EXE%" mkdir "%EXE%"
if not exist "%EXE%\apps" mkdir "%EXE%\apps"

if /i "%CMD%"=="init-exe" goto :init_exe
if /i "%CMD%"=="apps"     goto :apps
if /i "%CMD%"=="app"      goto :app
if /i "%CMD%"=="platform" goto :platform
if /i "%CMD%"=="shared"   goto :shared
if /i "%CMD%"=="config"   goto :config
if /i "%CMD%"=="versions" goto :versions
if /i "%CMD%"=="assemble" goto :assemble
if /i "%CMD%"=="verify"   goto :verify
if /i "%CMD%"=="status"   goto :status
if /i "%CMD%"=="pack"     goto :pack
echo [ERROR] _deploy_ops.bat: unknown command "%CMD%"
echo         Known: init-exe ^| apps ^| app ^<name^> ^| platform ^| shared ^| config ^| versions ^| assemble ^| verify ^| status ^| pack
exit /b 1

rem ============================================================
:assemble
call :busy_check
call :init_exe
if errorlevel 1 goto :fail
call :apps
if errorlevel 1 goto :fail
call :platform
if errorlevel 1 goto :fail
call :shared
if errorlevel 1 goto :fail
call :config
if errorlevel 1 goto :fail
call :versions
if errorlevel 1 goto :fail
echo.
echo [OK] assemble: exe\ rebuilt ^(apps + platform + shared + config^)
exit /b 0

rem ============================================================
rem  init-exe: skeleton from exe_template, generated layers wiped.
rem  exe\config and exe\VERSIONS.txt content are rebuilt by their own
rem  steps; exe\*.bat are refreshed from the template every time.
rem ============================================================
:init_exe
if not exist "%TPL%\run.bat" (
    echo [ERROR] exe_template\run.bat missing - cannot recreate exe\
    exit /b 1
)
if not exist "%EXE%\run.bat" (
    echo [INFO] exe\ skeleton missing - recreating from exe_template
    xcopy "%TPL%\*" "%EXE%\" /E /Y /I /H >nul
    if errorlevel 1 (
        echo [ERROR] xcopy exe_template -^> exe failed
        exit /b 1
    )
)
rem refresh the three launcher scripts from the template every time
xcopy "%TPL%\run.bat" "%EXE%\" /Y >nul
xcopy "%TPL%\start_all.bat" "%EXE%\" /Y >nul
xcopy "%TPL%\stop_all.bat" "%EXE%\" /Y >nul
rem wipe the generated layers (clean slate); exe\config is NOT touched
for %%L in (apps platform shared) do (
    if exist "%EXE%\%%L" rmdir /s /q "%EXE%\%%L"
)
mkdir "%EXE%\apps" 2>nul
mkdir "%EXE%\platform" 2>nul
mkdir "%EXE%\shared" 2>nul
rem old-layout leftovers
if exist "%EXE%\bin" rmdir /s /q "%EXE%\bin"
if exist "%EXE%\lib" rmdir /s /q "%EXE%\lib"
exit /b 0

rem ============================================================
rem  apps: <proj>\output\exe -> exe\apps\<name>   (MIRROR, no filters:
rem  by design the app folder content == the project's output\exe)
rem ============================================================
:apps
call :copy_app io     "%ROOT%\module\io\output\exe"
call :copy_app motion "%ROOT%\module\motion\output\exe"
call :copy_app re     "%ROOT%\module\re\output\exe"
call :copy_app mctrl  "%ROOT%\mctrl\output\exe"
call :copy_app gui    "%ROOT%\gui\output\exe"
if "%FAILED%"=="1" exit /b 1
exit /b 0

rem ============================================================
rem  app <name>: deploy exactly one app, change nothing else.
rem    No init-exe, no config, no platform/shared, no other app folder -
rem    this is the "I fixed mctrl, I only want mctrl in the package" path.
rem ============================================================
:app
set "APPN=%~2"
if "%APPN%"=="" (
    echo [ERROR] usage: _deploy_ops.bat app ^<io^|motion^|re^|mctrl^|gui^>
    exit /b 1
)
set "SRC="
if /i "%APPN%"=="io"     set "SRC=%ROOT%\module\io\output\exe"
if /i "%APPN%"=="motion" set "SRC=%ROOT%\module\motion\output\exe"
if /i "%APPN%"=="re"     set "SRC=%ROOT%\module\re\output\exe"
if /i "%APPN%"=="mctrl"  set "SRC=%ROOT%\mctrl\output\exe"
if /i "%APPN%"=="gui"    set "SRC=%ROOT%\gui\output\exe"
if not defined SRC (
    echo [ERROR] unknown app "%APPN%" - known: io motion re mctrl gui
    exit /b 1
)
call :copy_app %APPN% "%SRC%"
if "%FAILED%"=="1" exit /b 1
rem keep the package-side version record in step with what was just deployed
call :versions
exit /b 0

:copy_app
set "APPN=%~1"
set "SRC=%~2"
set "DST=%EXE%\apps\%APPN%"
if not exist "%SRC%" (
    echo [ERROR] %APPN%: deploy shape not found: %SRC%
    echo         build it first:  engineering\scripts\build_exe.bat %APPN%
    set "FAILED=1"
    exit /b 1
)
robocopy "%SRC%" "%DST%" /MIR %RCOPT% >nul
if errorlevel 8 (
    echo [ERROR] %APPN%: copy failed - file locked? run exe\stop_all.bat first
    set "FAILED=1"
    exit /b 1
)
echo [apps] %APPN%  ^<-  %SRC%
exit /b 0

rem ============================================================
rem  platform: platform_output -> exe\platform   (MIRROR)
rem    CTL_TL_RM excluded : another build - re builds the authoritative one
rem                         (2.0.4+: it sits under CTL_Platform\, /XD matches
rem                          the name at any depth)
rem    Platform excluded   : byte-identical to basiclibrary_output\lib
rem    Resource excluded   : one CTL.ico, no reader
rem    Config excluded     : platform sample config tree - the package root is
rem                          exe\config\ (PLATFORM_PATH); two config trees in
rem                          one package is exactly what we removed
rem    tool_*.bat/ps1 ...  : the platform team's own delivery tools
rem ============================================================
:platform
call :plat_mirror basiclibrary_output\lib "%PLATOUT%\basiclibrary_output\lib"
if errorlevel 1 exit /b 1
call :plat_mirror rpc_output "%PLATOUT%\rpc_output" /XD .git
if errorlevel 1 exit /b 1
call :plat_mirror exe_output "%PLATOUT%\exe_output" /XD CTL_TL_RM Platform Resource Config .git /XF tool_link.ps1 tool_start.bat tool_stop.bat delete_tools.bat
if errorlevel 1 exit /b 1
exit /b 0

:plat_mirror
set "NAME=%~1"
set "SRC=%~2"
rem rebuild the raw tail arguments (%3.. are the /XD //XF filters)
set "TAIL="
:pm_tail
if "%~3"=="" goto :pm_go
set "TAIL=%TAIL% %~3"
shift
goto :pm_tail
:pm_go
if not exist "%SRC%" (
    echo [ERROR] platform source missing: %SRC%
    echo         run: engineering\scripts\update_platform.bat
    set "FAILED=1"
    exit /b 1
)
robocopy "%SRC%" "%EXE%\platform\%NAME%" /MIR %RCOPT%%TAIL% >nul
if errorlevel 8 (
    echo [ERROR] platform mirror failed: %NAME% - file locked?
    set "FAILED=1"
    exit /b 1
)
echo [platform] exe\platform\%NAME%  ^<-  %SRC%
exit /b 0

rem ============================================================
rem  shared: runtime\ (in-repo machine blob) -> exe\shared   (MIRROR)
rem ============================================================
:shared
if not exist "%RUNTIME%\qt\Qt5WebEngineCore.dll" (
    echo [ERROR] runtime\qt incomplete - %RUNTIME%\qt\Qt5WebEngineCore.dll missing
    echo         runtime\ is the in-repo source of exe\shared - see runtime\README.md
    exit /b 1
)
robocopy "%RUNTIME%\qt" "%EXE%\shared\qt" /MIR %RCOPT% >nul
if errorlevel 8 (
    echo [ERROR] shared mirror failed: qt
    exit /b 1
)
robocopy "%RUNTIME%\system" "%EXE%\shared\system" /MIR %RCOPT% >nul
if errorlevel 8 (
    echo [ERROR] shared mirror failed: system
    exit /b 1
)
echo [shared] exe\shared\qt + exe\shared\system  ^<-  runtime\
exit /b 0

rem ============================================================
rem  config: config\ -> exe\config (single root, A6 first)
rem    exe\config is the PLATFORM_PATH root run.bat exports, so this step is
rem    the whole "field config" layer. Two classes of files:
rem      shipped - repo is the source of truth, refreshed on every assemble
rem      field   - the running process rewrites it (machine constants, camera
rem                calibration, recipes, licence, appconfig language). These are
rem                seeded only when missing and NEVER overwritten by an assemble.
rem    Upgrading a <=2.0.3 package: the old cwd-style dirs (config\<proc>\) are
rem    first MERGED into their new <kind>_config home (copy, no overwrite) and
rem    only then removed - field data must survive the layout change.
rem ============================================================
:config
if not exist "%ROOT%\engineering\config\mw_config.yaml" (
    echo [ERROR] config\mw_config.yaml not found
    exit /b 1
)
if not exist "%ROOT%\engineering\config\appconfig.yaml" (
    echo [ERROR] config\appconfig.yaml not found
    exit /b 1
)
if exist "%ROOT%\mctrl\config\MC_WM_Schema.json" (
    if exist "%ROOT%\engineering\config\mc_config\MachineConstants" (
        copy /Y "%ROOT%\mctrl\config\MC_WM_Schema.json" "%ROOT%\engineering\config\mc_config\MachineConstants\MC_WM_Schema.json" >nul
        if errorlevel 1 ( echo [ERROR] A6: copy MC_WM_Schema.json failed & exit /b 1 )
        echo [config] A6: mctrl\config\MC_WM_Schema.json  to  config\mc_config\MachineConstants
    )
)
if not exist "%EXE%\config" mkdir "%EXE%\config"

rem ---- 1) legacy cwd-style dirs -> new <kind>_config (merge, then remove) ----
rem    The one thing that cannot be moved automatically: mctrl used to resolve
rem    appDataPath "./App_Data" against its cwd, so field data sits inside the
rem    legacy dir while the new appDataPath is an absolute machine path. Warn
rem    instead of guessing.
if exist "%EXE%\config\mctrl\App_Data\" (
    echo [WARN] legacy mctrl data dir found: config\mctrl\App_Data
    echo        mctrl now writes to appconfig.yaml appDataPath instead.
    echo        Move that directory there by hand if it holds field data.
)
call :merge_legacy "%EXE%\config\eh"    "%EXE%\config\eh_config"
if errorlevel 1 exit /b 1
call :merge_legacy "%EXE%\config\ua"    "%EXE%\config\ua_config"
if errorlevel 1 exit /b 1
call :merge_legacy "%EXE%\config\st"    "%EXE%\config\st_config"
if errorlevel 1 exit /b 1
call :merge_legacy "%EXE%\config\mc"    "%EXE%\config\mc_config"
if errorlevel 1 exit /b 1
call :merge_legacy "%EXE%\config\rp"    "%EXE%\config\rp_config"
if errorlevel 1 exit /b 1
call :merge_legacy "%EXE%\config\io"    "%EXE%\config\io_config"
if errorlevel 1 exit /b 1
call :merge_legacy "%EXE%\config\re"    "%EXE%\config\rm_config"
if errorlevel 1 exit /b 1
call :merge_legacy "%EXE%\config\gui"   "%EXE%\config\gui_config"
if errorlevel 1 exit /b 1
call :merge_legacy "%EXE%\config\mctrl" "%EXE%\config\mctrl_config"
if errorlevel 1 exit /b 1
for %%N in (1 2 3 4) do call :merge_legacy "%EXE%\config\motion\config%%N" "%EXE%\config\motion_config\motion%%N"
if errorlevel 1 exit /b 1
if exist "%EXE%\config\motion\" rmdir /s /q "%EXE%\config\motion"
rem config\mt only ever held appconfig.yaml, which is the shared root file now
if exist "%EXE%\config\mt\" (
    rmdir /s /q "%EXE%\config\mt"
    echo [config] removed legacy config\mt ^(appconfig.yaml is the root file now^)
)
rem retired from the repo config root - robocopy /E never deletes, so drop it
rem from packages assembled before the cleanup
if exist "%EXE%\config\threadpool_config.yaml" (
    del /Q "%EXE%\config\threadpool_config.yaml"
    echo [config] removed retired file: config\threadpool_config.yaml
)

rem ---- 2) shipped files: refresh, but leave the field-owned paths alone ----
rem    /XD names match at any depth.
robocopy "%ROOT%\engineering\config" "%EXE%\config" /E %RCOPT% ^
    /XD MachineConstants CameraConfig recipes license_data Log ^
    /XF appconfig.yaml safety_interference.json >nul
if errorlevel 8 (
    echo [ERROR] copy config failed - file locked? run exe\stop_all.bat first
    exit /b 1
)

rem ---- 3) field-owned files: seed only when the destination is missing ----
for %%F in (appconfig.yaml mctrl_config\safety_interference.json) do (
    if not exist "%EXE%\config\%%F" if exist "%ROOT%\engineering\config\%%F" (
        copy /Y "%ROOT%\engineering\config\%%F" "%EXE%\config\%%F" >nul
        echo [config] seeded field file: config\%%F
    )
)
for %%D in (mc_config\MachineConstants mctrl_config\CameraConfig rm_config\recipes gui_config\license_data) do (
    if not exist "%EXE%\config\%%D\" if exist "%ROOT%\engineering\config\%%D\" (
        robocopy "%ROOT%\engineering\config\%%D" "%EXE%\config\%%D" /E %RCOPT% >nul
        echo [config] seeded field dir: config\%%D
    )
)
rem schema files sit inside field-owned dirs but are shipped, not field state:
rem A6 rewrote config\mc_config\MachineConstants\MC_WM_Schema.json in the repo,
rem so refresh that one file even though its directory is kept as-is
if exist "%ROOT%\engineering\config\mc_config\MachineConstants\MC_WM_Schema.json" (
    if not exist "%EXE%\config\mc_config\MachineConstants" mkdir "%EXE%\config\mc_config\MachineConstants"
    copy /Y "%ROOT%\engineering\config\mc_config\MachineConstants\MC_WM_Schema.json" ^
        "%EXE%\config\mc_config\MachineConstants\MC_WM_Schema.json" >nul
)
echo [config] config\  to  exe\config  ^(shipped refreshed, field data kept^)
exit /b 0

rem ============================================================
rem  merge_legacy <old dir> <new dir>
rem    Copy what is missing into the new home without overwriting anything
rem    already there (/XO), then drop the old dir. A copy failure keeps the
rem    old dir and aborts the assemble - never a silent data loss.
rem ============================================================
:merge_legacy
if not exist "%~1\" exit /b 0
if not exist "%~2\" mkdir "%~2"
robocopy "%~1" "%~2" /E /XO %RCOPT% >nul
if errorlevel 8 (
    echo [ERROR] config migration failed: %~1  -^>  %~2
    echo         legacy dir kept - run exe\stop_all.bat and retry
    exit /b 1
)
rmdir /s /q "%~1"
echo [config] migrated legacy %~nx1  -^>  %~nx2
exit /b 0

rem ============================================================
rem  versions: exe\VERSIONS.txt
rem ============================================================
:versions
call :collect_tags
set "VERFILE=%EXE%\VERSIONS.txt"
> "%VERFILE%" echo 1927project package
>> "%VERFILE%" echo assembled: %DATE% %TIME%
>> "%VERFILE%" echo.
>> "%VERFILE%" echo app                source                  version
>> "%VERFILE%" echo --------------------------------------------------------------------
for %%A in ("io=module/io" "motion=module/motion" "re=module/re" "mctrl=mctrl" "gui=gui") do (
    for /f "tokens=1,2 delims==" %%a in ("%%~A") do (
        call :ver_line "%%a" "%%b" "%ROOT%\%%b"
    )
)
>> "%VERFILE%" echo.
>> "%VERFILE%" echo platform layer    : platform_output mirror ^(see tags below^)
>> "%VERFILE%" echo   basiclibrary_output : %TAG_BASIC%
>> "%VERFILE%" echo   rpc_output         : %TAG_RPC%
>> "%VERFILE%" echo   exe_output         : %TAG_EXE%
>> "%VERFILE%" echo   re platform source : %PLAT_COMMIT%  ^(module\re\platform^)
>> "%VERFILE%" echo shared layer      : runtime\ ^(in-repo Qt blob + VC runtime^)
echo [versions] exe\VERSIONS.txt written
exit /b 0

:ver_line
set "VNAME=%~1"
set "VREL=%~2"
set "VABS=%~3"
set "TAG="
if exist "%VABS%\.git" (
    for /f %%t in ('git -C "%VABS%" describe --tags --always 2^>nul') do set "TAG=%%t"
)
if not defined TAG set "TAG=n/a"
set "VNAME_P=%VNAME%                  "
set "VREL_P=%VREL%                      "
>> "%VERFILE%" echo %VNAME_P:~0,18% %VREL_P:~0,22% %TAG%
exit /b 0

rem ============================================================
rem  verify: hard checks on the assembled tree
rem ============================================================
:verify
call :collect_tags
set "VFAIL=0"
for %%A in ("io|CTL_MDL_IO.exe" "motion|CTL_MDL_Motion.exe" "re|CTL_TL_RM.exe" "mctrl|MCtrl_WM.exe" "gui|GUI_WM.exe") do (
    for /f "tokens=1,2 delims=|" %%a in ("%%~A") do (
        if exist "%EXE%\apps\%%a\%%b" (
            echo   [ok] apps\%%a : %%b
        ) else (
            echo [ERROR] verify: apps\%%a - %%b missing
            set "VFAIL=1"
        )
    )
)
rem external CTL programs may legitimately be absent
for %%A in ("CTL_Platform\CTL_SE_EH\bin\Release\CTL_SE_EH.exe" "CTL_Platform\CTL_TL_MC\bin\Release\CTL_TL_MC.exe" "CTL_Platform\CTL_TL_RP\bin\Release\CTL_TL_RP.exe" "CTL_Platform\CTL_SE_UA\bin\Release\CTL_SE_UA.exe" "CTL_Platform\CTL_TL_MT\bin\Release\CTL_TL_MT.exe") do (
    if exist "%EXE%\platform\exe_output\%%~A" (
        echo   [ok] platform\exe_output\%%~A
    ) else (
        echo   [warn] platform\exe_output\%%~A not present
    )
)
rem single config root: shared files at the root, one dir per process kind
for %%C in ("appconfig.yaml" "mw_config.yaml" ^
            "eh_config" "mc_config" "rp_config" "ua_config" "st_config" ^
            "io_config" "motion_config\motion1" "motion_config\motion2" "motion_config\motion3" "motion_config\motion4" ^
            "mctrl_config" "gui_config" "rm_config" "rm_config\Recipe_Schema.json" "mc_config\MachineConstants") do (
    if exist "%EXE%\config\%%~C\" (
        echo   [ok] config\%%~C
    ) else if exist "%EXE%\config\%%~C" (
        echo   [ok] config\%%~C
    ) else (
        echo [ERROR] verify: missing config\%%~C
        set "VFAIL=1"
    )
)
rem reverse check: the legacy cwd-style tree must be gone, otherwise two config
rem trees coexist and it is never clear which one the process read
for %%L in (eh ua st mc rp io re gui mctrl motion mt) do (
    if exist "%EXE%\config\%%L\" (
        echo [ERROR] verify: legacy config dir still present: config\%%L
        set "VFAIL=1"
    )
)
for %%F in ("ISTAR middleware|%EXE%\platform\basiclibrary_output\lib\ISTARMW.dll" "ISTAR appconfig|%EXE%\platform\basiclibrary_output\lib\ISTARAppConfig.dll" "Qt core|%EXE%\platform\basiclibrary_output\lib\Qt5Core.dll" "Qt platform plugin|%EXE%\platform\basiclibrary_output\lib\platforms\qwindows.dll" "libUIRm|%EXE%\platform\rpc_output\libUIRM\lib\libUIRm.dll" "WebEngine module|%EXE%\shared\qt\Qt5WebEngineCore.dll" "imageformat plugin qsvg|%EXE%\shared\qt\imageformats\qsvg.dll" "VC runtime|%EXE%\shared\system\msvcp140.dll" "field config|%EXE%\config\mw_config.yaml") do (
    for /f "tokens=1,2 delims=|" %%a in ("%%~F") do (
        if exist "%%b" (
            echo   [ok] %%a
        ) else (
            echo [ERROR] verify: missing %%a
            set "VFAIL=1"
        )
    )
)
echo.
if "%VFAIL%"=="1" (
    echo [ERROR] verify failed - the assembled tree is incomplete
    exit /b 1
)
echo [OK] verify: exe\ looks complete
exit /b 0

rem ============================================================
rem  status: file counts + the platform tags in use
rem ============================================================
:status
call :collect_tags
echo.
echo ============================================================
echo  exe\ status
echo ============================================================
if not exist "%EXE%\apps" echo   exe\apps not found - run: engineering\scripts\build_exe.bat
if exist "%EXE%\apps" for /d %%d in ("%EXE%\apps\*") do (
    set /a CNT=0
    for /f %%f in ('dir /b /a-d "%%d" 2^>nul') do set /a CNT+=1
    echo   apps\%%~nxd  :  !CNT! items
)
echo   platform tags   :  basic=%TAG_BASIC%  rpc=%TAG_RPC%  exe=%TAG_EXE%
echo   re platform src :  %PLAT_COMMIT%
echo ============================================================
exit /b 0

rem ============================================================
rem  pack: zip exe\ -> dist\1927-exe-<ver>.zip
rem ============================================================
:pack
set "VER=%~2"
if "%VER%"=="" (
    for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmm"') do set "VER=%%t"
)
if "%VER%"=="" set "VER=unknown"
if not exist "%ROOT%\dist" mkdir "%ROOT%\dist"
set "ZIP=%ROOT%\dist\1927-exe-%VER%.zip"
if exist "%ZIP%" del "%ZIP%"
echo [pack] compressing exe\ -^> %ZIP%
powershell -NoProfile -Command "Compress-Archive -Path '%EXE%\*' -DestinationPath '%ZIP%' -Force"
if errorlevel 1 (
    echo [ERROR] pack failed
    exit /b 1
)
echo [OK] pack: %ZIP%
exit /b 0

rem ============================================================
rem  helpers
rem ============================================================
:collect_tags
set "TAG_BASIC=no tag"
set "TAG_RPC=no tag"
set "TAG_EXE=no tag"
set "PLAT_COMMIT=n/a"
if exist "%PLATOUT%\basiclibrary_output\.git" for /f %%t in ('git -C "%PLATOUT%\basiclibrary_output" describe --tags --abbrev^=0 2^>nul') do set "TAG_BASIC=%%t"
if exist "%PLATOUT%\rpc_output\.git" for /f %%t in ('git -C "%PLATOUT%\rpc_output" describe --tags --abbrev^=0 2^>nul') do set "TAG_RPC=%%t"
if exist "%PLATOUT%\exe_output\.git" for /f %%t in ('git -C "%PLATOUT%\exe_output" describe --tags --abbrev^=0 2^>nul') do set "TAG_EXE=%%t"
if exist "%ROOT%\module\re\platform\.git" for /f %%h in ('git -C "%ROOT%\module\re\platform" rev-parse --short HEAD 2^>nul') do set "PLAT_COMMIT=%%h"
exit /b 0

:busy_check
for %%p in (MCtrl_WM.exe GUI_WM.exe CTL_SE_EH.exe CTL_SE_SM.exe CTL_SE_TR.exe CTL_SE_UA.exe CTL_TL_MC.exe CTL_TL_PS.exe CTL_TL_RP.exe CTL_TL_RM.exe CTL_MDL_IO.exe CTL_MDL_Motion.exe) do (
    tasklist /FI "IMAGENAME eq %%p" 2>nul | find /I "%%p" >nul
    if not errorlevel 1 (
        echo [WARN] %%p is still running - files under exe\ may be locked.
        echo        Run exe\stop_all.bat first.
    )
)
exit /b 0

:fail
echo [ERROR] assemble aborted - fix the [ERROR] line above and run again
exit /b 1
