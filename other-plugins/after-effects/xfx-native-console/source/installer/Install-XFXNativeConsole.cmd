@echo off
setlocal
if not exist "%~dp0XFXNativeConsoleInstaller.exe" (
  echo Missing XFXNativeConsoleInstaller.exe beside this file.
  exit /b 1
)
"%~dp0XFXNativeConsoleInstaller.exe" %*
exit /b %errorlevel%
