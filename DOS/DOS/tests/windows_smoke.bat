@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
if not exist dos.exe exit /b 1

dos.exe verify pe dos.exe
if errorlevel 1 exit /b %errorlevel%

dos.exe run tests\stage5.dos
if errorlevel 1 exit /b %errorlevel%

dos.exe build tests\stage5.dos -o tests\stage5_windows.exe
if errorlevel 1 exit /b %errorlevel%

dos.exe verify package tests\stage5_windows.exe
if errorlevel 1 exit /b %errorlevel%

tests\stage5_windows.exe
if errorlevel 1 exit /b %errorlevel%

echo WINDOWS_SMOKE=PASS
endlocal
