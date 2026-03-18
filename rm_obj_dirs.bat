@echo off
cd /d %~dp0
for /d %%D in (build_portable build_release_mingw build-briefutil-*) do (
    if exist "%%D" (
        echo Removing %%D ...
        del /f /s /q "%%D" 1>nul 2>nul
        rmdir /s /q "%%D" 2>nul
    )
)
if exist dist (
    echo Removing dist ...
    del /f /s /q dist 1>nul 2>nul
    rmdir /s /q dist 2>nul
)
echo Done.
