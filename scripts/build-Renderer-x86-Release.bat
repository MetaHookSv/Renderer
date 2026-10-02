@echo off
setlocal
set "Configuration=Release"
call "%~dp0build-Renderer-x86.bat" %*
exit /b %errorlevel%
