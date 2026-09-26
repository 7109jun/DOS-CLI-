@echo off
setlocal EnableExtensions
cd /d "%~dp0"

where x86_64-w64-mingw32-g++.exe >nul 2>&1
if errorlevel 1 (
    echo llvm-mingw or MinGW-w64 x86_64 compiler was not found.
    echo Install an x86_64 Windows GNU toolchain and put it in PATH.
    exit /b 1
)

cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++.exe
if errorlevel 1 exit /b %errorlevel%

cmake --build build-mingw --parallel
if errorlevel 1 exit /b %errorlevel%

if not exist "build-mingw\dos.exe" (
    echo DOS PE executable was not produced.
    exit /b 1
)

copy /Y "build-mingw\dos.exe" "dos.exe" >nul
if errorlevel 1 exit /b %errorlevel%

"dos.exe" verify pe "dos.exe"
if errorlevel 1 exit /b %errorlevel%

echo DOS llvm-mingw x64 PE build complete.
endlocal
