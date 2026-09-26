@echo off
setlocal EnableExtensions
cd /d "%~dp0"

where cmake >nul 2>&1
if errorlevel 1 (
    echo CMake was not found in PATH.
    exit /b 1
)

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b %errorlevel%

cmake --build build --config Release --parallel
if errorlevel 1 exit /b %errorlevel%

if not exist "build\Release\dos.exe" (
    echo DOS PE executable was not produced.
    exit /b 1
)

copy /Y "build\Release\dos.exe" "dos.exe" >nul
if errorlevel 1 exit /b %errorlevel%

if not exist "build\Release\plugins\python" (
    echo Python plugin is missing from the build output.
    exit /b 1
)

if not exist "plugins" mkdir "plugins"
xcopy /E /I /Y "build\Release\plugins" "plugins" >nul
if errorlevel 1 exit /b %errorlevel%

"dos.exe" --version
if errorlevel 1 exit /b %errorlevel%
"dos.exe" verify pe "dos.exe"
if errorlevel 1 exit /b %errorlevel%

"dos.exe" build "tests\stage5.dos" -o "build\stage5.exe"
if errorlevel 1 exit /b %errorlevel%
"dos.exe" verify package "build\stage5.exe"
if errorlevel 1 exit /b %errorlevel%

echo DOS Windows x64 Release PE build and package verification complete.
endlocal
