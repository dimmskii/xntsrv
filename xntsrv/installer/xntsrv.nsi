; XNTsrv installer.
;
; Copies the staged components (see stage.ps1) to Program Files and hands
; the machine-wide setup to xntsrv-register; the uninstaller calls
; xntsrv-unregister.  All setup logic lives in those tools, not here.
;
; Components: XNTsrv itself (always), audio, files for Linux guests, and the
; Start menu shortcut.  An upgrade offers the previous install's choices.
; Silent installs take them from the command line:
;   setup.exe /S [/NOAUDIO|/AUDIO] [/NOGUEST|/GUEST] [/NOSHORTCUT|/SHORTCUT]
; Without them, an upgrade keeps the previous choices and a new install takes
; everything.
;
; Build:  makensis /DVERSION=x.y.z /DSTAGE=<staged folder> /DOUTFILE=<setup.exe> xntsrv.nsi
;
; SPDX-License-Identifier: MIT

Unicode true
!include "LogicLib.nsh"
!include "Sections.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !error "pass /DVERSION=x.y.z"
!endif
!ifndef STAGE
  !error "pass /DSTAGE=<staged folder>"
!endif
!ifndef OUTFILE
  !error "pass /DOUTFILE=<setup.exe>"
!endif

!define PUBLISHER "Dmitri Poterianski"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\XNTsrv"
; Which components are installed, one DWORD each (1 or 0).  Under the key
; xntsrv-register writes, so xntsrv-unregister removes it too.
!define COMPKEY "SOFTWARE\XNTsrv\Components"
; Upstream's own icon and licence text, used as they are.
!define ICON "..\..\xorg-server\hw\xwin\X.ico"
!define LICENCE "..\..\COPYING"

Name "XNTsrv"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\XNTsrv"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
XPStyle on
Icon "${ICON}"
UninstallIcon "${ICON}"
LicenseData "${LICENCE}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "XNTsrv"
VIAddVersionKey "FileDescription" "XNTsrv Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "${PUBLISHER}; includes VcXsrv, see NOTICE.txt"

InstType "Full"
InstType "Minimal"

Page license
Page components
Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

; ---- remembering the choice ------------------------------------------

!macro RememberComponent SECTION NAME
  ${If} ${SectionIsSelected} ${SECTION}
    WriteRegDWORD HKLM "${COMPKEY}" "${NAME}" 1
  ${Else}
    WriteRegDWORD HKLM "${COMPKEY}" "${NAME}" 0
  ${EndIf}
!macroend

; Unselect SECTION if the previous install left it out (value 0); then the
; command line ($2) decides: /NO<OPTION> leaves it out, /<OPTION> puts it in.
!macro ChooseComponent SECTION NAME OPTION
  ClearErrors
  ReadRegDWORD $1 HKLM "${COMPKEY}" "${NAME}"
  ${IfNot} ${Errors}
  ${AndIf} $1 == 0
    !insertmacro UnselectSection ${SECTION}
  ${EndIf}
  ClearErrors
  ${GetOptions} $2 "/NO${OPTION}" $1
  ${IfNot} ${Errors}
    !insertmacro UnselectSection ${SECTION}
  ${EndIf}
  ClearErrors
  ${GetOptions} $2 "/${OPTION}" $1
  ${IfNot} ${Errors}
    !insertmacro SelectSection ${SECTION}
  ${EndIf}
!macroend

; ---- components ------------------------------------------------------

Section "XNTsrv" SecCore
  SectionIn RO 1 2
  SetRegView 64
  SetShellVarContext all

  ; Stop a previous install's services and programs so their files can be
  ; replaced.  Its component registry key goes with them, and was read in
  ; .onInit already.
  ${If} ${FileExists} "$INSTDIR\xntsrv-unregister.exe"
    nsExec::ExecToLog '"$INSTDIR\xntsrv-unregister.exe"'
    Pop $0
  ${EndIf}

  ; Remove the optional components; the sections below put back the chosen ones.
  RMDir /r "$INSTDIR\audio"
  RMDir /r "$INSTDIR\guest"
  Delete "$SMPROGRAMS\XNTsrv.lnk"

  SetOutPath "$INSTDIR"
  File /r "${STAGE}\core\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Audio" SecAudio
  SectionIn 1
  SetOutPath "$INSTDIR\audio"
  File /r "${STAGE}\audio\*.*"
SectionEnd

Section "Files for Linux guests" SecGuest
  SectionIn 1
  SetOutPath "$INSTDIR\guest"
  File /r "${STAGE}\guest\*.*"
SectionEnd

Section "Start menu shortcut" SecShortcut
  SectionIn 1 2
  SetShellVarContext all
  CreateShortCut "$SMPROGRAMS\XNTsrv.lnk" "$INSTDIR\xntsrvw.exe" "" "$INSTDIR\xntsrv-server.exe" 0
SectionEnd

; Last, once every chosen component's files are in place.
Section "-Register"
  SetRegView 64
  SetOutPath "$INSTDIR"

  nsExec::ExecToLog '"$INSTDIR\xntsrv-register.exe"'
  Pop $0
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "Setting up XNTsrv failed (xntsrv-register exit code $0).$\nThe details are in the installer's log window." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}

  !insertmacro RememberComponent ${SecAudio} "Audio"
  !insertmacro RememberComponent ${SecGuest} "Guest"
  !insertmacro RememberComponent ${SecShortcut} "Shortcut"

  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "XNTsrv"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNINSTKEY}" "URLInfoAbout" "https://github.com/dimmskii/xntsrv"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\xntsrv-server.exe"
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Function .onInit
  ; Upgrade in place: reuse the folder a previous install registered, and
  ; offer the components it had.
  SetRegView 64
  ReadRegStr $0 HKLM "SOFTWARE\XNTsrv" "InstallDir"
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
  ${GetParameters} $2
  !insertmacro ChooseComponent ${SecAudio} "Audio" "AUDIO"
  !insertmacro ChooseComponent ${SecGuest} "Guest" "GUEST"
  !insertmacro ChooseComponent ${SecShortcut} "Shortcut" "SHORTCUT"
FunctionEnd

Section "Uninstall"
  SetRegView 64
  SetShellVarContext all

  ; Only ever remove a folder that really is an XNTsrv install.
  ${IfNot} ${FileExists} "$INSTDIR\xntsrv-unregister.exe"
    MessageBox MB_OK|MB_ICONSTOP "$INSTDIR does not look like an XNTsrv installation; nothing was removed." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}

  ; Removes the services, firewall rule and registration; user settings are kept.
  nsExec::ExecToLog '"$INSTDIR\xntsrv-unregister.exe"'
  Pop $0

  Delete "$SMPROGRAMS\XNTsrv.lnk"
  DeleteRegKey HKLM "${UNINSTKEY}"
  RMDir /r "$INSTDIR"
SectionEnd
