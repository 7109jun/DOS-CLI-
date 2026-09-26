@echo off
setlocal EnableExtensions
set "DOS_EXE=%~dp0dos.exe"
if not exist "%DOS_EXE%" (
    echo dos.exe not found next to this script.
    exit /b 1
)

reg add "HKCU\Software\Classes\.dos" /ve /d "DOSFile" /f >nul
reg add "HKCU\Software\Classes\.dos" /v "Content Type" /d "text/x-dos" /f >nul
reg add "HKCU\Software\Classes\.dos\OpenWithProgids" /v "DOSFile" /t REG_NONE /d "" /f >nul
reg add "HKCU\Software\Classes\DOSFile" /ve /d "DOS Source File" /f >nul
reg add "HKCU\Software\Classes\DOSFile" /v "FriendlyTypeName" /d "DOS Source File" /f >nul
reg add "HKCU\Software\Classes\DOSFile\DefaultIcon" /ve /d "\"%DOS_EXE%\",0" /f >nul
reg add "HKCU\Software\Classes\DOSFile\shell\open\command" /ve /d "\"%DOS_EXE%\" \"%%1\"" /f >nul

ie4uinit.exe -show >nul 2>&1

echo .dos files are associated with DOS for the current Windows user.
echo Double-click a .dos file on the desktop to run it with dos.exe.
endlocal
