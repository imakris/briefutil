@echo off
REM ========================================================================
REM build_portable.bat - Build portable distribution of briefutil
REM ========================================================================
REM
REM Creates a self-contained portable directory under dist/ containing the
REM briefutil executable and all required Qt/MinGW runtime dependencies.
REM
REM Prerequisites:
REM   - Qt 6.10.1 with MinGW kit installed at C:\Qt\6.10.1\mingw_64
REM   - MinGW toolchain at C:\Qt\Tools\mingw1310_64
REM   - CMake at C:\Qt\Tools\CMake_64
REM   - Ninja at C:\Qt\Tools\Ninja
REM
REM ========================================================================

cd /d %~dp0

set QT_PREFIX=C:\Qt\6.10.1\mingw_64
set MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin
set CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe
set NINJA=C:\Qt\Tools\Ninja\ninja.exe
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
echo [1/4] Configuring CMake ...
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
echo [2/4] Building ...
"%CMAKE%" --build "%BUILD_DIR%" --config Release -j8
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

REM -- Assemble portable directory --
echo.
echo [3/4] Assembling portable distribution ...

set PORTABLE_DIR=%DIST_DIR%\portable
if exist "%PORTABLE_DIR%" rmdir /s /q "%PORTABLE_DIR%"
mkdir "%PORTABLE_DIR%"

REM The POST_BUILD steps in CMakeLists already ran windeployqt and copied
REM runtime DLLs into the build/app/ directory.  Copy only the distributable
REM files, not build artefacts (CMakeFiles, autogen, .obj, etc.).
copy /y "%BUILD_DIR%\app\briefutil.exe" "%PORTABLE_DIR%\" >nul
copy /y "%BUILD_DIR%\app\*.dll"         "%PORTABLE_DIR%\" >nul
for %%D in (platforms imageformats iconengines tls networkinformation generic qml qmltooling) do (
    if exist "%BUILD_DIR%\app\%%D" xcopy /e /i /q /y "%BUILD_DIR%\app\%%D" "%PORTABLE_DIR%\%%D" >nul
)

REM -- Build info --
echo.
echo [4/4] Writing build info ...

for /f %%i in ('git rev-parse --short HEAD 2^>nul') do set GIT_COMMIT=%%i
for /f %%i in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set GIT_BRANCH=%%i
set TIMESTAMP=%date% %time%

(
    echo Build timestamp: %TIMESTAMP%
    echo Git branch:      %GIT_BRANCH%
    echo Git commit:      %GIT_COMMIT%
    echo Configuration:   Release
    echo Toolchain:       MinGW ^(GCC^)
    echo Qt:              %QT_PREFIX%
) > "%PORTABLE_DIR%\briefutil_build_info.txt"

echo.
echo ========================================================================
echo Portable distribution ready at:
echo   %PORTABLE_DIR%
echo ========================================================================
