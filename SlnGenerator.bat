@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup\SetupTeam.ps1" %*
exit /b %ERRORLEVEL%
