@echo off
rem Yes I am sorry, but this is mostly made by ChatGPT
rem I am not an expert on .bat syntax (bash is better) and I just wanted this done
rem I would of prefered python but not everybody has that so this is the solution


setlocal EnableExtensions DisableDelayedExpansion
title EP3Restoration HL2 Content Extractor

cd /d "%~dp0" || (
    echo ERROR: Could not switch to the batch file directory.
    pause
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
set "OUTPUT=%SCRIPT_DIR%valve_assets_converted"
set "ZIP=%SCRIPT_DIR%recompiled_hl2_models.zip"
set "GAMEFILE=%TEMP%\source_sdk_game_path_%RANDOM%_%RANDOM%.txt"

echo ============================================================
echo    EP3Restoration HL2 Content Extractor by theaperturecat
echo ============================================================
echo.

if exist "%OUTPUT%" (
    echo ERROR: Output folder already exists:
    echo %OUTPUT%
    echo Remove or rename it before running this script again.
    echo A previous run may have left partially extracted files there.
    pause
    exit /b 1
)

echo Searching for Steam...
set "STEAM="

for /f "usebackq delims=" %%S in (`powershell.exe -NoLogo -NoProfile -Command "$p=(Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).InstallPath; if (!$p) {$p=(Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -ErrorAction SilentlyContinue).InstallPath}; if (!$p) {$p=(Get-ItemProperty 'HKLM:\SOFTWARE\Valve\Steam' -ErrorAction SilentlyContinue).InstallPath}; if ($p) {[Console]::WriteLine($p)}"`) do set "STEAM=%%S"

if not defined STEAM (
    echo ERROR: Steam installation not found.
    pause
    exit /b 1
)

echo Steam: "%STEAM%"
echo.
echo Searching Steam libraries for Source SDK Base 2013 Singleplayer and Alien Swarm...

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $steam=$env:STEAM; $libraries=@($steam); $vdf=Join-Path $steam 'steamapps\libraryfolders.vdf'; if(Test-Path -LiteralPath $vdf){$content=Get-Content -LiteralPath $vdf -Raw; foreach($m in [regex]::Matches($content,'(?m)^\s*\x22path\x22\s+\x22(.+?)\x22')){$libraries += $m.Groups[1].Value.Replace('\\','\')}; foreach($m in [regex]::Matches($content,'(?m)^\s*\x22\d+\x22\s+\x22(.+?)\x22')){$libraries += $m.Groups[1].Value.Replace('\\','\')}}; $found=$null; $alienSwarm=$false; foreach($library in ($libraries | Select-Object -Unique)){if(!(Test-Path -LiteralPath $library -PathType Container)){continue}; $common=Join-Path $library 'steamapps\common'; $candidate=Join-Path $common 'Source SDK Base 2013 Singleplayer'; if(!$found -and (Test-Path -LiteralPath $candidate -PathType Container)){$found=$candidate}; if(Test-Path -LiteralPath (Join-Path $common 'Alien Swarm') -PathType Container){$alienSwarm=$true}}; if(!$found){exit 2}; if(!$alienSwarm){exit 3}; [IO.File]::WriteAllText($env:GAMEFILE,$found,[Text.Encoding]::Default); exit 0"

if errorlevel 3 (
    del "%GAMEFILE%" >nul 2>&1
    echo ERROR: Alien Swarm was not found in any Steam library.
    pause
    exit /b 1
)

if errorlevel 2 (
    del "%GAMEFILE%" >nul 2>&1
    echo ERROR: Source SDK Base 2013 Singleplayer was not found in any Steam library.
    pause
    exit /b 1
)

if errorlevel 1 (
    del "%GAMEFILE%" >nul 2>&1
    echo ERROR: Steam library search failed.
    pause
    exit /b 1
)

if not exist "%GAMEFILE%" (
    echo ERROR: Steam library search did not return an installation path.
    pause
    exit /b 1
)

set /p "GAME="<"%GAMEFILE%"
del "%GAMEFILE%" >nul 2>&1
if not defined GAME (
    echo ERROR: The Source SDK installation path was empty.
    pause
    exit /b 1
)

echo Found: "%GAME%"
echo.

for %%G in (hl2 episodic ep2) do (
    if not exist "%GAME%\%%G\" (
        echo ERROR: Required game content folder is missing:
        echo %GAME%\%%G
        pause
        exit /b 1
    )
)

mkdir "%OUTPUT%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Could not create the output folder:
    echo %OUTPUT%
    pause
    exit /b 1
)

for %%G in (hl2 episodic ep2) do (
    echo.
    echo ==========================================
    echo Processing %%G
    echo ==========================================
    call :Extract "%%G"
    if errorlevel 1 (
        echo ERROR: Extraction of %%G failed.
        echo Partial output has been left at:
        echo %OUTPUT%
        pause
        exit /b 1
    )
)

echo.
echo ==========================================
echo Deleting old shaders
echo ==========================================

rmdir /s /q "%OUTPUT%\hl2\hl2_misc\shaders\"


echo.
echo ==========================================
echo Checking Python installation...
echo ==========================================

where python >nul 2>&1
if errorlevel 1 (
    where py >nul 2>&1
    if errorlevel 1 (
        echo ERROR: Python is not installed.
        echo Please install Python 3 and try again.
        pause
        exit /b 1
    )
    set "PYTHON=py"
) else (
    set "PYTHON=python"
)

echo Python found:
%PYTHON% --version

echo.
echo Checking pip...

%PYTHON% -m pip --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: pip is not installed.
    echo Attempting to install pip...
    %PYTHON% -m ensurepip --upgrade
    if errorlevel 1 (
        echo ERROR: Failed to install pip.
        pause
        exit /b 1
    )
)

echo.
echo Installing valvemdl...

%PYTHON% -m pip install --upgrade valvemdl
if errorlevel 1 (
    echo ERROR: Failed to install valvemdl.
    pause
    exit /b 1
)

echo.
echo Python dependencies successfully checked.



echo.
echo ==========================================
echo Converting MDL v43-48 to v49
echo ==========================================

del /q conversion_errors.txt 2>nul

%PYTHON% mdl_v43_48_to_v49_fixed.py "%OUTPUT%\hl2\hl2_misc\models" --output "%OUTPUT%\hl2_v49_models\models"
%PYTHON% mdl_v43_48_to_v49_fixed.py "%OUTPUT%\episodic\ep1_pak\models" --output "%OUTPUT%\ep1_v49_models\models"
%PYTHON% mdl_v43_48_to_v49_fixed.py "%OUTPUT%\ep2\ep2_pak\models" --output "%OUTPUT%\ep2_v49_models\models"


echo.
echo ==========================================
echo Deleting old v43-48 models
echo ==========================================

rmdir /s /q "%OUTPUT%\hl2\hl2_misc\models\"
rmdir /s /q "%OUTPUT%\episodic\ep1_pak\models\"
rmdir /s /q "%OUTPUT%\ep2\ep2_pak\models\"

mkdir "%OUTPUT%\hl2_pre_anniversary_update_maps\maps\"

echo.
echo ==========================================
echo SUCCESS!
echo ==========================================
echo Extracted content:
echo %OUTPUT%
echo.
echo.
echo REMEMBER:
echo To install HL2 maps (EP1/EP2 maps are already extracted from the source sdk) copy your HL2 maps to %OUTPUT%\hl2_pre_anniversary_update_maps\maps\. 
echo The maps you copy need to be pre-anniversary maps (you can get these from the steam_legacy branch of HL2), the 20th anniversary update changed some things with maps.
echo.
pause
exit /b 0


:Extract
set "SRC=%GAME%\%~1"
set "DST=%OUTPUT%\%~1"

if not exist "%SRC%\" (
    echo ERROR: Source folder does not exist: %SRC%
    exit /b 1
)

rem A split VPK is opened through its *_dir.vpk index. Keep its numbered
rem companion chunks beside it; vpk.exe reads those chunks automatically.
dir /b /a-d "%SRC%\*_dir.vpk" >nul 2>&1
if errorlevel 1 (
    echo ERROR: No *_dir.vpk archives were found in:
    echo %SRC%
    exit /b 1
)

mkdir "%DST%" >nul 2>&1
if not exist "%DST%\" (
    echo ERROR: Could not create destination folder: %DST%
    exit /b 1
)

echo Extracting all files from VPK directory archives...
set "VPK_FAILED=0"
for %%V in ("%SRC%\*_dir.vpk") do (
    if exist "%%~fV" (
        echo.
        echo Extracting %%~nxV
        rem Valve's documented whole-archive syntax is: vpk <vpkfile>
        rem Do NOT use 'x' without filenames: 'x' extracts selected files only.
        vpkeditcli -e "/" -o "%DST%" "%%~fV" 
        if errorlevel 1 (
            echo ERROR: vpkeditcli.exe failed on %%~nxV
            set "VPK_FAILED=1"
        )
    )
)

if "%VPK_FAILED%"=="1" exit /b 1

echo.
echo Copying loose files from %~1...
robocopy "%SRC%" "%DST%" /E /COPY:DAT /DCOPY:DAT /R:2 /W:1 /XJ /XF *.vpk *.vpk.sound.cache /NFL /NDL /NJH /NJS /NP
if errorlevel 8 exit /b 1

exit /b 0
