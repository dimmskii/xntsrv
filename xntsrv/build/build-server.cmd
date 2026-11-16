@echo off
rem Builds the X server from source, one stage at a time, with the same steps
rem as upstream's buildall.sh but driven from cmd instead of WSL or Cygwin.
rem
rem   build-server.cmd <stage> [jobs]
rem
rem Stages, in order: freetype openssl pthreads mhmake server package
rem Run from the repository root.  Expects the tools upstream's makefiles call
rem by fixed path: c:\winflexbison and c:\Python39.
rem
rem SPDX-License-Identifier: MIT
setlocal enabledelayedexpansion

set STAGE=%1
set JOBS=%2
if "%JOBS%"=="" set JOBS=4

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio with the C++ tools was not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

rem What upstream's setenv.sh exports.
set "MHMAKECONF=%CD%"
set IS64=1
set PYTHON3=c:\Python39\python.exe
set CFLAGS=-FS
set "PATH=%CD%\tools\mhmake\Release64;%PATH%"

if "%STAGE%"=="freetype" goto freetype
if "%STAGE%"=="openssl" goto openssl
if "%STAGE%"=="pthreads" goto pthreads
if "%STAGE%"=="mhmake" goto mhmake
if "%STAGE%"=="server" goto server
if "%STAGE%"=="package" goto package
echo usage: build-server.cmd freetype^|openssl^|pthreads^|mhmake^|server^|package [jobs]
exit /b 2

:freetype
MSBuild.exe freetype\MSBuild.sln -t:Build -p:Configuration=Release -p:Platform=x64 -m:%JOBS% -nologo -v:minimal
exit /b %ERRORLEVEL%

:openssl
if not exist openssl\release64 mkdir openssl\release64
cd openssl\release64
rem OpenSSL's own tests are not needed, and compiling them in parallel
rem sometimes fails on a locked debug-information file.  A second run picks up
rem where a failed one stopped.
perl.exe ..\Configure VC-WIN64A --release no-tests || exit /b 1
jom.exe /J%JOBS% || jom.exe /J%JOBS%
exit /b %ERRORLEVEL%

:pthreads
cd pthreads
nmake.exe /nologo VC-static
exit /b %ERRORLEVEL%

:mhmake
MSBuild.exe tools\mhmake\mhmakevc10.sln -t:Build -p:Configuration=Release -p:Platform=x64 -m:%JOBS% -nologo -v:minimal
exit /b %ERRORLEVEL%

:server
mhmake.exe -P%JOBS% -C xorg-server MAKESERVER=1
exit /b %ERRORLEVEL%

:package
rem What upstream's packageall.sh does for the 64-bit release build: add the
rem Visual C++ runtime and run upstream's own installer script.  The result is
rem upstream's installer, which XNTsrv's installer then repackages.
cd xorg-server\installer
for %%f in (msvcp140.dll vcruntime140.dll vcruntime140_1.dll) do (
    copy /y "%VCToolsRedistDir%x64\Microsoft.VC143.CRT\%%f" . >nul || exit /b 1
)
del /q vcxsrv-64.*.installer*.exe 2>nul
"%ProgramFiles(x86)%\NSIS\makensis.exe" /V2 vcxsrv-64.nsi
exit /b %ERRORLEVEL%
