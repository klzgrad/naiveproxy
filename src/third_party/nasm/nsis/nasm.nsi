#!Nsis Installer Command Script

#
# Copyright (c) 2009, Shao Miller (shao.miller@yrdsb.edu.on.ca)
# Copyright (c) 2009, Cyrill Gorcunov (gorcunov@gmail.com)
# All rights reserved.
#
# The script requires NSIS v2.45 (or any later)
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <copyright holder> BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

!addincludedir "${objdir}\nsis"
!addincludedir "${srcdir}\nsis"
!include "version.nsh"
!include "arch.nsh"

!define PRODUCT_NAME "Netwide Assembler"
!define PRODUCT_SHORT_NAME "nasm"
!define PACKAGE_NAME "${PRODUCT_NAME} ${VERSION}"
!define PACKAGE_SHORT_NAME "${PRODUCT_SHORT_NAME}-${VERSION}"

SetCompressor /solid lzma

!if "${NSIS_PACKEDVERSION}" >= 0x3000000
Unicode true
!endif

!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_INSTALLMODE_INSTDIR "NASM"
!include "NASMMultiUser.nsh"

!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MULTIUSER_INSTALLMODEPAGE_INTERFACE

;--------------------------------
;General

;Name and file
Name "${PACKAGE_NAME}"
OutFile "${objdir}\${PACKAGE_SHORT_NAME}-installer-${ARCH}.exe"

;Get installation folder from registry if available
InstallDirRegKey HKCU "Software\${PRODUCT_SHORT_NAME}" ""

;Request application privileges for Windows Vista
RequestExecutionLevel user

;--------------------------------
;Variables

Var StartMenuFolder
Var CmdFailed

;--------------------------------
;Interface Settings
Caption "${PACKAGE_SHORT_NAME} installation"
Icon "${srcdir}\nsis\nasm.ico"
UninstallIcon "${srcdir}\nsis\nasm-un.ico"

!define MUI_ABORTWARNING

;--------------------------------
;Pages

!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY

;Start Menu Folder Page Configuration
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKCU"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\${PRODUCT_SHORT_NAME}"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "${PRODUCT_SHORT_NAME}"

!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder

!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
;Installer Sections

!insertmacro MUI_LANGUAGE English

Section "NASM" SecNasm
    Sectionin RO
    SetOutPath "$INSTDIR"
    File "${srcdir}\LICENSE"
    File "${objdir}\nasm.exe"
    File "${objdir}\ndisasm.exe"
    File "${srcdir}\nsis\nasm.ico"

    ;Store installation folder
    WriteRegStr HKCU "Software\${PRODUCT_SHORT_NAME}" "" $INSTDIR

    ;Store shortcuts folder
    WriteRegStr HKCU "Software\${PRODUCT_SHORT_NAME}\" "lnk" $SMPROGRAMS\$StartMenuFolder
    WriteRegStr HKCU "Software\${PRODUCT_SHORT_NAME}\" "bat-lnk" $DESKTOP\${PRODUCT_SHORT_NAME}.lnk

    ;
    ; the bat we need
    StrCpy $CmdFailed "true"
    FileOpen $0 "nasmpath.bat" w
    IfErrors skip
    StrCpy $CmdFailed "false"
    FileWrite $0 "@set path=$INSTDIR;%path%$\r$\n"
    FileWrite $0 "@%comspec%"
    FileClose $0
    CreateShortCut "$DESKTOP\${PRODUCT_SHORT_NAME}.lnk" "$INSTDIR\nasmpath.bat" "" "$INSTDIR\nasm.ico" 0
skip:
    ;Create uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    !insertmacro MUI_STARTMENU_WRITE_BEGIN Application

    ;Create shortcuts
    CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
    StrCmp $CmdFailed "true" +2
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\${PRODUCT_SHORT_NAME}-shell.lnk" "$INSTDIR\nasmpath.bat"
    CreateShortCut  "$SMPROGRAMS\$StartMenuFolder\${PRODUCT_SHORT_NAME}.lnk" "$INSTDIR\nasm.exe" "" "$INSTDIR\nasm.ico" 0
    CreateShortCut  "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" "$INSTDIR\Uninstall.exe"

    !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

Section "RDOFF" SecRdoff
    File "${objdir}\rdoff\ldrdf.exe"
    File "${objdir}\rdoff\rdf2bin.exe"
    File "${objdir}\rdoff\rdf2com.exe"
    File "${objdir}\rdoff\rdf2ith.exe"
    File "${objdir}\rdoff\rdf2ihx.exe"
    File "${objdir}\rdoff\rdf2srec.exe"
    File "${objdir}\rdoff\rdfdump.exe"
    File "${objdir}\rdoff\rdflib.exe"
SectionEnd

Section "Manual" SecManual
    SetOutPath "$INSTDIR"
    File "${objdir}\doc\nasmdoc.pdf"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Manual.lnk" "$INSTDIR\nasmdoc.pdf"
SectionEnd

Section "VS8 integration" SecVS8
    CreateDirectory "$INSTDIR\VSrules"
    SetOutPath "$INSTDIR\VSrules"
    File "${srcdir}\contrib\VSrules\nasm.README"
    File "${srcdir}\contrib\VSrules\nasm.rules"
SectionEnd

;--------------------------------
;Descriptions

    ;Language strings
    LangString DESC_SecNasm ${LANG_ENGLISH}     "NASM assembler and disassember modules"
    LangString DESC_SecManual ${LANG_ENGLISH}   "Complete NASM manual (pdf file)"
    LangString DESC_SecRdoff ${LANG_ENGLISH}    "RDOFF utilities (you may not need it if you don't know what is it)"
    LangString DESC_SecVS8 ${LANG_ENGLISH}      "Visual Studio 2008 NASM integration (rules file)"

    ;Assign language strings to sections
    !insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecNasm} $(DESC_SecNasm)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecRdoff} $(DESC_SecRdoff)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecManual} $(DESC_SecManual)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecVS8} $(DESC_SecVS8)
    !insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
;Uninstaller Section

Section "Uninstall"
    ;
    ; files on HDD
    IfFileExists "$INSTDIR" +3 +1
        MessageBox MB_OK "No files found, aborting."
        Abort
        MessageBox MB_YESNO "The following directory will be deleted$\n$INSTDIR" IDYES rm_instdir_true IDNO rm_instdir_false
        rm_instdir_true:
            RMDir /r /rebootok "$INSTDIR"
        rm_instdir_false:

    ;
    ; Desktop link
    ReadRegStr $0 HKCU Software\${PRODUCT_SHORT_NAME} "bat-lnk"
    StrCmp $0 0 +1 +3
        MessageBox MB_OK "Invalid path to a bat-lnk file, aborting"
        Abort
    IfFileExists $0 +3 +1
        MessageBox MB_OK "No bat-lnk files found, aborting."
        Abort
        MessageBox MB_YESNO "The following file will be deleted$\n$0" IDYES rm_batlinks_true IDNO rm_batlinks_false
        rm_batlinks_true:
            Delete /rebootok "$0"
            RMDir "$0"
        rm_batlinks_false:

    ;
    ; Start menu folder
    ReadRegStr $0 HKCU Software\${PRODUCT_SHORT_NAME} "lnk"
    StrCmp $0 0 +1 +3
        MessageBox MB_OK "Invalid path to a lnk file, aborting"
        Abort
    IfFileExists $0 +3 +1
        MessageBox MB_OK "No lnk files found, aborting."
        Abort
        MessageBox MB_YESNO "The following directory will be deleted$\n$0" IDYES rm_links_true IDNO rm_links_false
        rm_links_true:
            Delete /rebootok "$0\*"
            RMDir "$0"
        rm_links_false:
    DeleteRegKey /ifempty HKCU "Software\${PRODUCT_SHORT_NAME}"
SectionEnd

;
; MUI requires this hooks
Function .onInit
    SetRegView ${BITS}
    !insertmacro MULTIUSER_INIT
FunctionEnd

Function un.onInit
    !insertmacro MULTIUSER_UNINIT
FunctionEnd
