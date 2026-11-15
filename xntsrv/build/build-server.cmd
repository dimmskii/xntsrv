@echo off
rem Builds the X server from source, one stage at a time, with the same steps
rem as upstream's buildall.sh but driven from cmd instead of WSL or Cygwin.
rem
rem   build-server.cmd <stage> [jobs]
rem
rem Stages, in order: freetype openssl pthreads mhmake server
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
echo usage: build-server.cmd freetype^|openssl^|pthreads^|mhmake^|server [jobs]
exit /b 2

:freetype
MSBuild.exe freetype\MSBuild.sln -t:Build -p:Configuration=Release -p:Platform=x64 -m:%JOBS% -nologo -v:minimal
exit /b %ERRORLEVEL%

:openssl
if not exist openssl\release64 mkdir openssl\release64
cd openssl\release64
perl.exe ..\Configure VC-WIN64A --release || exit /b 1
jom.exe /J%JOBS%
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
