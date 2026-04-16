@echo off
REM ========================================================================
REM build_portable.bat - Build portable distribution of briefutil
REM ========================================================================
REM
REM Creates a self-contained portable directory under dist/ with a single
REM visible briefutil.exe launcher at the top level and a briefutil_runtime\
REM directory containing the actual application binary and all Qt/MinGW
REM runtime dependencies.
REM
REM Requires a build_config.bat file with local tool paths.
REM See build_config.bat.example for a template.
REM
REM ========================================================================

cd /d %~dp0

REM -- Load local build configuration --
if not exist "%~dp0build_config.bat" (
    echo ERROR: build_config.bat not found.
    echo.
    echo Copy build_config.bat.example to build_config.bat and set the paths
    echo for your local Qt / MinGW installation.  For example:
    echo.
    echo   set QT_PREFIX=C:\Qt\6.10.1\mingw_64
    echo   set MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin
    echo   set CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe
    echo   set NINJA=C:\Qt\Tools\Ninja\ninja.exe
    echo.
    exit /b 1
)
call "%~dp0build_config.bat"

set WINDEPLOYQT=%QT_PREFIX%\bin\windeployqt.exe
set QML_DIR=%~dp0briefutil\app\qml

set BUILD_DIR=%~dp0build_portable
set DIST_DIR=%~dp0dist

REM -- Verify prerequisites --
if not exist "%CMAKE%" (
    echo ERROR: CMake not found at %CMAKE%
    exit /b 1
)
if not exist "%MINGW_BIN%\g++.exe" (
    echo ERROR: MinGW g++ not found at %MINGW_BIN%\g++.exe
    exit /b 1
)
if not exist "%QT_PREFIX%\bin\qmake.exe" (
    echo ERROR: Qt kit not found at %QT_PREFIX%
    exit /b 1
)

REM -- Configure --
echo.
echo [1/5] Configuring CMake ...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

"%CMAKE%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="%QT_PREFIX%" ^
    -DCMAKE_CXX_COMPILER="%MINGW_BIN%\g++.exe" ^
    -DCMAKE_C_COMPILER="%MINGW_BIN%\gcc.exe" ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -S "%~dp0briefutil" ^
    -B "%BUILD_DIR%"
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

REM -- Build --
echo.
echo [2/5] Building ...
"%CMAKE%" --build "%BUILD_DIR%" --config Release -j8
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

REM -- Assemble portable directory --
echo.
echo [3/5] Assembling portable distribution ...

set PORTABLE_DIR=%DIST_DIR%\portable
set RUNTIME_DIR=%PORTABLE_DIR%\briefutil_runtime
if exist "%PORTABLE_DIR%" rmdir /s /q "%PORTABLE_DIR%"
mkdir "%PORTABLE_DIR%"
mkdir "%RUNTIME_DIR%"

REM The POST_BUILD steps in CMakeLists already ran windeployqt and copied
REM runtime DLLs into the build/app/ directory.  Copy only the distributable
REM files, not build artefacts (CMakeFiles, autogen, .obj, etc.).
copy /y "%BUILD_DIR%\app\briefutil_portable_launcher.exe" "%PORTABLE_DIR%\briefutil.exe" >nul
if errorlevel 1 (
    echo ERROR: briefutil_portable_launcher.exe not found in build output.
    exit /b 1
)
copy /y "%BUILD_DIR%\app\briefutil.exe" "%RUNTIME_DIR%\briefutil.exe" >nul
if errorlevel 1 (
    echo ERROR: briefutil.exe not found in build output.
    exit /b 1
)
copy /y "%BUILD_DIR%\app\*.dll" "%RUNTIME_DIR%\" >nul

set MISSING_DIRS=
for %%D in (platforms imageformats iconengines tls networkinformation generic qml qmltooling) do (
    if exist "%BUILD_DIR%\app\%%D" (
        xcopy /e /i /q /y "%BUILD_DIR%\app\%%D" "%RUNTIME_DIR%\%%D" >nul
        if errorlevel 1 (
            echo WARNING: Failed to copy plugin directory %%D
        )
    )
)
if not exist "%RUNTIME_DIR%\platforms" (
    echo ERROR: Critical plugin directory 'platforms' is missing.
    exit /b 1
)

REM -- Build info --
echo.
echo [4/5] Writing build info ...

for /f %%i in ('git rev-parse --short HEAD 2^>nul') do set GIT_COMMIT=%%i
for /f %%i in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set GIT_BRANCH=%%i
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd_HH:mm"') do set TIMESTAMP=%%i

(
    echo Build timestamp: %TIMESTAMP%
    echo Git branch:      %GIT_BRANCH%
    echo Git commit:      %GIT_COMMIT%
    echo Configuration:   Release
    echo Toolchain:       MinGW ^(GCC^)
    echo Qt:              %QT_PREFIX%
) > "%RUNTIME_DIR%\briefutil_build_info.txt"

REM -- Create ZIP archive --
echo.
echo [5/5] Creating ZIP archive ...

for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyy_MMM_dd_HHmm"') do set STAMP=%%i
set ZIP_NAME=briefutil_%STAMP%_w64.zip
set ZIP_PATH=%DIST_DIR%\%ZIP_NAME%
if exist "%ZIP_PATH%" del /q "%ZIP_PATH%"

powershell -NoProfile -Command "Compress-Archive -Path '%PORTABLE_DIR%\*' -DestinationPath '%ZIP_PATH%' -Force"
if errorlevel 1 (
    echo WARNING: ZIP creation failed. Portable directory is still available.
) else (
    echo Created: %ZIP_PATH%
)

echo.
echo ========================================================================
echo Portable distribution ready at:
echo   %PORTABLE_DIR%
echo   %ZIP_PATH%
echo ========================================================================
