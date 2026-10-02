@echo off
setlocal
set "Configuration=Debug"
call "%~dp0build-Renderer-x86.bat" %*
exit /b %errorlevel%
