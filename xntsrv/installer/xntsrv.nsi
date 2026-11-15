; XNTSrv installer.
;
; Copies a staged folder (see stage.ps1) to Program Files and hands the
; machine-wide setup to xntsrv-register; the uninstaller calls
; xntsrv-unregister.  All setup logic lives in those tools, not here.
;
; Build:  makensis /DVERSION=x.y.z /DSTAGE=<staged folder> /DOUTFILE=<setup.exe> xntsrv.nsi
;
; SPDX-License-Identifier: MIT

Unicode true
!include "LogicLib.nsh"

!ifndef VERSION
  !error "pass /DVERSION=x.y.z"
!endif
!ifndef STAGE
  !error "pass /DSTAGE=<staged folder>"
!endif
!ifndef OUTFILE
  !error "pass /DOUTFILE=<setup.exe>"
!endif

!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\XNTSrv"
; Upstream's own icon and licence text, used as they are.
!define ICON "..\..\xorg-server\hw\xwin\X.ico"
!define LICENCE "..\..\COPYING"

Name "XNTSrv"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\XNTSrv"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
XPStyle on
Icon "${ICON}"
UninstallIcon "${ICON}"
LicenseData "${LICENCE}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "XNTSrv"
VIAddVersionKey "FileDescription" "XNTSrv Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "XNTSrv contributors; includes VcXsrv, see NOTICE.txt"

Page license
Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

Function .onInit
  ; Upgrade in place: reuse the folder a previous install registered.
  SetRegView 64
  ReadRegStr $0 HKLM "SOFTWARE\XNTSrv" "InstallDir"
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
FunctionEnd

Section "XNTSrv"
  SetRegView 64
  SetShellVarContext all

  ; Stop a previous install's services so their files can be replaced.
  ${If} ${FileExists} "$INSTDIR\xntsrv-unregister.exe"
    nsExec::ExecToLog '"$INSTDIR\xntsrv-unregister.exe"'
    Pop $0
  ${EndIf}

  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  nsExec::ExecToLog '"$INSTDIR\xntsrv-register.exe"'
  Pop $0
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "Setting up XNTSrv failed (xntsrv-register exit code $0).$\nThe details are in the installer's log window." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}

  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "XNTSrv"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "XNTSrv contributors"
  WriteRegStr HKLM "${UNINSTKEY}" "URLInfoAbout" "https://github.com/dimmskii/xntsrv"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\xntsrv-server.exe"
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1

  CreateShortCut "$SMPROGRAMS\XNTSrv.lnk" "$INSTDIR\xntsrvw.exe" "" "$INSTDIR\xntsrv-server.exe" 0
SectionEnd

Section "Uninstall"
  SetRegView 64
  SetShellVarContext all

  ; Only ever remove a folder that really is an XNTSrv install.
  ${IfNot} ${FileExists} "$INSTDIR\xntsrv-unregister.exe"
    MessageBox MB_OK|MB_ICONSTOP "$INSTDIR does not look like an XNTSrv installation; nothing was removed." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}

  ; Removes the services, firewall rule and registration; user settings are kept.
  nsExec::ExecToLog '"$INSTDIR\xntsrv-unregister.exe"'
  Pop $0

  Delete "$SMPROGRAMS\XNTSrv.lnk"
  DeleteRegKey HKLM "${UNINSTKEY}"
  RMDir /r "$INSTDIR"
SectionEnd
