@echo off
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0"
if errorlevel 1 exit /b 1

set "LIFELINE_TOOLS_ROOT=%CD%\dev\source-build\tools"
set "LIFELINE_DOWNLOAD_ROOT=%LIFELINE_TOOLS_ROOT%\downloads"
set "LIFELINE_BUILD_ROOT=%CD%\dev\installer-native\cmake"
set "LIFELINE_OUTPUT_ROOT=%CD%\build"

echo [1/4] Preparing the x64 Visual Studio compiler environment...
call :prepare_compiler
if errorlevel 1 goto failed

echo [2/4] Preparing pinned CMake and Ninja tools...
call :prepare_tools
if errorlevel 1 goto failed
set "LIFELINE_CMAKE=%LIFELINE_TOOLS_ROOT%\cmake-4.3.3\cmake-4.3.3-windows-x86_64\bin\cmake.exe"
set "PATH=%LIFELINE_TOOLS_ROOT%\ninja-1.13.2;%LIFELINE_TOOLS_ROOT%\cmake-4.3.3\cmake-4.3.3-windows-x86_64\bin;%PATH%"

echo [3/4] Configuring the release installer...
"%LIFELINE_CMAKE%" -S "project\installer" -B "%LIFELINE_BUILD_ROOT%" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DLIFELINE_OUTPUT=%LIFELINE_OUTPUT_ROOT%"
if errorlevel 1 goto failed

echo [4/4] Building the native game, movie codecs and Installer.exe...
"%LIFELINE_CMAKE%" --build "%LIFELINE_BUILD_ROOT%" --target LifelineSetup --parallel 2
if errorlevel 1 goto failed
if not exist "%LIFELINE_OUTPUT_ROOT%\Installer.exe" (
    echo ERROR: The build did not produce Installer.exe.
    goto failed
)

echo.
echo Build complete: "%LIFELINE_OUTPUT_ROOT%\Installer.exe"
echo Run Installer.exe and select your original SLUS_208.48 Lifeline ISO to install.
popd
if /i not "%~1"=="--no-pause" pause
exit /b 0

:failed
echo.
echo Installer build failed. See the error above.
popd
if /i not "%~1"=="--no-pause" pause
exit /b 1

:prepare_compiler
where cl.exe >nul 2>nul
if not errorlevel 1 goto check_compiler
set "LIFELINE_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%LIFELINE_VSWHERE%" goto missing_tools
for /f "usebackq tokens=*" %%i in (`"%LIFELINE_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "LIFELINE_VS=%%i"
if not defined LIFELINE_VS goto missing_tools
call "%LIFELINE_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1

:check_compiler
if defined VSCMD_ARG_TGT_ARCH if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    echo ERROR: Use an x64 Native Tools Command Prompt or open this batch file from Explorer.
    exit /b 1
)
for %%T in (cl.exe rc.exe git.exe curl.exe powershell.exe certutil.exe) do (
    where %%T >nul 2>nul
    if errorlevel 1 (
        echo ERROR: Required build tool was not found: %%T
        exit /b 1
    )
)
exit /b 0

:missing_tools
echo ERROR: Install Visual Studio Build Tools with Desktop development with C++ and a Windows SDK.
echo Git for Windows is also required to compile the installer movie codecs.
exit /b 1

:prepare_tools
if not exist "%LIFELINE_DOWNLOAD_ROOT%" mkdir "%LIFELINE_DOWNLOAD_ROOT%"
if errorlevel 1 exit /b 1

set "LIFELINE_TOOL_ID=cmake-4.3.3"
set "LIFELINE_TOOL_URL=https://github.com/Kitware/CMake/releases/download/v4.3.3/cmake-4.3.3-windows-x86_64.zip"
set "LIFELINE_TOOL_HASH=935ADE9E5E8723583C07F44C5592CEA2A1C8F65C56CA7E07B34C025C880E0BD6"
set "LIFELINE_TOOL_EXE=cmake-4.3.3-windows-x86_64\bin\cmake.exe"
call :ensure_tool
if errorlevel 1 exit /b 1

set "LIFELINE_TOOL_ID=ninja-1.13.2"
set "LIFELINE_TOOL_URL=https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip"
set "LIFELINE_TOOL_HASH=07FC8261B42B20E71D1720B39068C2E14FFCEE6396B76FB7A795FB460B78DC65"
set "LIFELINE_TOOL_EXE=ninja.exe"
call :ensure_tool
if errorlevel 1 exit /b 1
exit /b 0

:ensure_tool
set "LIFELINE_TOOL_DEST=%LIFELINE_TOOLS_ROOT%\%LIFELINE_TOOL_ID%"
set "LIFELINE_ARCHIVE_PATH=%LIFELINE_DOWNLOAD_ROOT%\%LIFELINE_TOOL_ID%.zip"
if not exist "%LIFELINE_ARCHIVE_PATH%" goto download_tool
call :verify_archive
if not errorlevel 1 goto extract_tool
echo %LIFELINE_TOOL_ID%: cached archive failed verification; downloading again.

:download_tool
echo %LIFELINE_TOOL_ID%: downloading...
curl.exe --fail --location --retry 3 --output "%LIFELINE_ARCHIVE_PATH%" "%LIFELINE_TOOL_URL%"
if errorlevel 1 (
    echo ERROR: Download failed: %LIFELINE_TOOL_URL%
    exit /b 1
)
call :verify_archive
if errorlevel 1 exit /b 1

:extract_tool
if exist "%LIFELINE_TOOL_DEST%\%LIFELINE_TOOL_EXE%" exit /b 0
echo %LIFELINE_TOOL_ID%: extracting...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath $env:LIFELINE_ARCHIVE_PATH -DestinationPath $env:LIFELINE_TOOL_DEST -Force"
if errorlevel 1 (
    echo ERROR: Archive extraction failed: "%LIFELINE_ARCHIVE_PATH%"
    exit /b 1
)
if not exist "%LIFELINE_TOOL_DEST%\%LIFELINE_TOOL_EXE%" (
    echo ERROR: Tool extraction did not produce %LIFELINE_TOOL_EXE%.
    exit /b 1
)
exit /b 0

:verify_archive
set "LIFELINE_ACTUAL_HASH="
for /f "skip=1 tokens=* delims=" %%H in ('certutil.exe -hashfile "%LIFELINE_ARCHIVE_PATH%" SHA256 2^>nul') do if not defined LIFELINE_ACTUAL_HASH set "LIFELINE_ACTUAL_HASH=%%H"
set "LIFELINE_ACTUAL_HASH=%LIFELINE_ACTUAL_HASH: =%"
if /i not "%LIFELINE_ACTUAL_HASH%"=="%LIFELINE_TOOL_HASH%" (
    echo ERROR: SHA256 mismatch for %LIFELINE_TOOL_ID%.
    echo Expected: %LIFELINE_TOOL_HASH%
    echo Actual:   %LIFELINE_ACTUAL_HASH%
    exit /b 1
)
exit /b 0
