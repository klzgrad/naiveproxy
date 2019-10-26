/*

MultiUser.nsh

Installer configuration for multi-user Windows environments

Copyright 2008-2009 Joost Verburg
Updated 2016 by H. Peter Anvin to handle 64-bit Windows

*/

!ifndef MULTIUSER_INCLUDED
!define MULTIUSER_INCLUDED
!verbose push
!verbose 3

;Standard NSIS header files

!ifdef MULTIUSER_MUI
  !include MUI2.nsh
!endif
!include nsDialogs.nsh
!include LogicLib.nsh
!include WinVer.nsh
!include FileFunc.nsh

;Variables

Var MultiUser.Privileges
Var MultiUser.InstallMode

;Command line installation mode setting

!ifdef MULTIUSER_INSTALLMODE_COMMANDLINE
  !include StrFunc.nsh
  !ifndef StrStr_INCLUDED
    ${StrStr}
  !endif
  !ifndef MULTIUSER_NOUNINSTALL
    !ifndef UnStrStr_INCLUDED
      ${UnStrStr}
    !endif
  !endif

  Var MultiUser.Parameters
  Var MultiUser.Result
!endif

;Installation folder stored in registry

!ifdef MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY & MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME
  Var MultiUser.InstDir
!endif

!ifdef MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY & MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME
  Var MultiUser.DefaultKeyValue
!endif

;Windows Vista UAC setting

!if "${MULTIUSER_EXECUTIONLEVEL}" == Admin
  RequestExecutionLevel admin
  !define MULTIUSER_EXECUTIONLEVEL_ALLUSERS
!else if "${MULTIUSER_EXECUTIONLEVEL}" == Power
  RequestExecutionLevel admin
  !define MULTIUSER_EXECUTIONLEVEL_ALLUSERS
!else if "${MULTIUSER_EXECUTIONLEVEL}" == Highest
  RequestExecutionLevel highest
  !define MULTIUSER_EXECUTIONLEVEL_ALLUSERS
!else
  RequestExecutionLevel user
!endif

/*

Install modes

*/

!macro MULTIUSER_INSTALLMODE_ALLUSERS UNINSTALLER_PREFIX UNINSTALLER_FUNCPREFIX

  ;Install mode initialization - per-machine

  ${ifnot} ${IsNT}
    ${orif} $MultiUser.Privileges == "Admin"
    ${orif} $MultiUser.Privileges == "Power"

    StrCpy $MultiUser.InstallMode AllUsers

    SetShellVarContext all

    !if "${UNINSTALLER_PREFIX}" != UN
      ;Set default installation location for installer
      !ifdef MULTIUSER_INSTALLMODE_INSTDIR
        StrCpy $INSTDIR "${GLOBALINSTDIR}\${MULTIUSER_INSTALLMODE_INSTDIR}"
      !endif
    !endif

    !ifdef MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY & MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME

      ReadRegStr $MultiUser.InstDir HKLM "${MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY}" "${MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME}"

      ${if} $MultiUser.InstDir != ""
        StrCpy $INSTDIR $MultiUser.InstDir
      ${endif}

    !endif

    !ifdef MULTIUSER_INSTALLMODE_${UNINSTALLER_PREFIX}FUNCTION
      Call "${MULTIUSER_INSTALLMODE_${UNINSTALLER_PREFIX}FUNCTION}"
    !endif

  ${endif}

!macroend

!macro MULTIUSER_INSTALLMODE_CURRENTUSER UNINSTALLER_PREFIX UNINSTALLER_FUNCPREFIX

  ;Install mode initialization - per-user

  ${if} ${IsNT}

    StrCpy $MultiUser.InstallMode CurrentUser

    SetShellVarContext current

    !if "${UNINSTALLER_PREFIX}" != UN
      ;Set default installation location for installer
      !ifdef MULTIUSER_INSTALLMODE_INSTDIR
        ${if} ${AtLeastWin2000}
          StrCpy $INSTDIR "$LOCALAPPDATA\bin\${MULTIUSER_INSTALLMODE_INSTDIR}"
        ${else}
          StrCpy $INSTDIR "${GLOBALINSTDIR}\${MULTIUSER_INSTALLMODE_INSTDIR}"
        ${endif}
      !endif
    !endif

    !ifdef MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY & MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME

      ReadRegStr $MultiUser.InstDir HKCU "${MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY}" "${MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME}"

      ${if} $MultiUser.InstDir != ""
        StrCpy $INSTDIR $MultiUser.InstDir
      ${endif}

    !endif

    !ifdef MULTIUSER_INSTALLMODE_${UNINSTALLER_PREFIX}FUNCTION
      Call "${MULTIUSER_INSTALLMODE_${UNINSTALLER_PREFIX}FUNCTION}"
    !endif

  ${endif}

!macroend

Function MultiUser.InstallMode.AllUsers
  !insertmacro MULTIUSER_INSTALLMODE_ALLUSERS "" ""
FunctionEnd

Function MultiUser.InstallMode.CurrentUser
  !insertmacro MULTIUSER_INSTALLMODE_CURRENTUSER "" ""
FunctionEnd

!ifndef MULTIUSER_NOUNINSTALL

Function un.MultiUser.InstallMode.AllUsers
  !insertmacro MULTIUSER_INSTALLMODE_ALLUSERS UN .un
FunctionEnd

Function un.MultiUser.InstallMode.CurrentUser
  !insertmacro MULTIUSER_INSTALLMODE_CURRENTUSER UN .un
FunctionEnd

!endif

/*

Installer/uninstaller initialization

*/

!macro MULTIUSER_INIT_QUIT UNINSTALLER_FUNCPREFIX

  !ifdef MULTIUSER_INIT_${UNINSTALLER_FUNCPREFIX}FUNCTIONQUIT
    Call "${MULTIUSER_INIT_${UNINSTALLER_FUNCPREFIX}FUCTIONQUIT}
  !else
    Quit
  !endif

!macroend

!macro MULTIUSER_INIT_TEXTS

  !ifndef MULTIUSER_INIT_TEXT_ADMINREQUIRED
    !define MULTIUSER_INIT_TEXT_ADMINREQUIRED "$(^Caption) requires Administrator priviledges."
  !endif

  !ifndef MULTIUSER_INIT_TEXT_POWERREQUIRED
    !define MULTIUSER_INIT_TEXT_POWERREQUIRED "$(^Caption) requires at least Power User priviledges."
  !endif

  !ifndef MULTIUSER_INIT_TEXT_ALLUSERSNOTPOSSIBLE
    !define MULTIUSER_INIT_TEXT_ALLUSERSNOTPOSSIBLE "Your user account does not have sufficient privileges to install $(^Name) for all users of this compuetr."
  !endif

!macroend

!macro MULTIUSER_INIT_CHECKS UNINSTALLER_PREFIX UNINSTALLER_FUNCPREFIX

  ;Installer initialization - check privileges and set install mode

  !insertmacro MULTIUSER_INIT_TEXTS

  UserInfo::GetAccountType
  Pop $MultiUser.Privileges

  ${if} ${IsNT}

    ;Check privileges

    !if "${MULTIUSER_EXECUTIONLEVEL}" == Admin

      ${if} $MultiUser.Privileges != "Admin"
        MessageBox MB_OK|MB_ICONSTOP "${MULTIUSER_INIT_TEXT_ADMINREQUIRED}"
        !insertmacro MULTIUSER_INIT_QUIT "${UNINSTALLER_FUNCPREFIX}"
      ${endif}

    !else if "${MULTIUSER_EXECUTIONLEVEL}" == Power

      ${if} $MultiUser.Privileges != "Power"
        ${andif} $MultiUser.Privileges != "Admin"
        ${if} ${AtMostWinXP}
           MessageBox MB_OK|MB_ICONSTOP "${MULTIUSER_INIT_TEXT_POWERREQUIRED}"
        ${else}
           MessageBox MB_OK|MB_ICONSTOP "${MULTIUSER_INIT_TEXT_ADMINREQUIRED}"
        ${endif}
        !insertmacro MULTIUSER_INIT_QUIT "${UNINSTALLER_FUNCPREFIX}"
      ${endif}

    !endif

    !ifdef MULTIUSER_EXECUTIONLEVEL_ALLUSERS

      ;Default to per-machine installation if possible

      ${if} $MultiUser.Privileges == "Admin"
        ${orif} $MultiUser.Privileges == "Power"
        !ifndef MULTIUSER_INSTALLMODE_DEFAULT_CURRENTUSER
          Call ${UNINSTALLER_FUNCPREFIX}MultiUser.InstallMode.AllUsers
        !else
          Call ${UNINSTALLER_FUNCPREFIX}MultiUser.InstallMode.CurrentUser
        !endif

        !ifdef MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY & MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME

          ;Set installation mode to setting from a previous installation

          !ifndef MULTIUSER_INSTALLMODE_DEFAULT_CURRENTUSER
            ReadRegStr $MultiUser.DefaultKeyValue HKLM "${MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY}" "${MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME}"
            ${if} $MultiUser.DefaultKeyValue == ""
              ReadRegStr $MultiUser.DefaultKeyValue HKCU "${MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY}" "${MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME}"
              ${if} $MultiUser.DefaultKeyValue != ""
                Call ${UNINSTALLER_FUNCPREFIX}MultiUser.InstallMode.CurrentUser
              ${endif}
            ${endif}
          !else
            ReadRegStr $MultiUser.DefaultKeyValue HKCU "${MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY}" "${MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME}"
            ${if} $MultiUser.DefaultKeyValue == ""
              ReadRegStr $MultiUser.DefaultKeyValue HKLM "${MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY}" "${MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME}"
              ${if} $MultiUser.DefaultKeyValue != ""
                Call ${UNINSTALLER_FUNCPREFIX}MultiUser.InstallMode.AllUsers
              ${endif}
            ${endif}
          !endif

        !endif

      ${else}
        Call ${UNINSTALLER_FUNCPREFIX}MultiUser.InstallMode.CurrentUser
      ${endif}

    !else

      Call ${UNINSTALLER_FUNCPREFIX}MultiUser.InstallMode.CurrentUser

    !endif

    !ifdef MULTIUSER_INSTALLMODE_COMMANDLINE

      ;Check for install mode setting on command line

      ${${UNINSTALLER_FUNCPREFIX}GetParameters} $MultiUser.Parameters

      ${${UNINSTALLER_PREFIX}StrStr} $MultiUser.Result $MultiUser.Parameters "/CurrentUser"

      ${if} $MultiUser.Result != ""
        Call ${UNINSTALLER_FUNCPREFIX}MultiUser.InstallMode.CurrentUser
      ${endif}

      ${${UNINSTALLER_PREFIX}StrStr} $MultiUser.Result $MultiUser.Parameters "/AllUsers"

      ${if} $MultiUser.Result != ""
        ${if} $MultiUser.Privileges == "Admin"
          ${orif} $MultiUser.Privileges == "Power"
          Call ${UNINSTALLER_FUNCPREFIX}MultiUser.InstallMode.AllUsers
        ${else}
          MessageBox MB_OK|MB_ICONSTOP "${MULTIUSER_INIT_TEXT_ALLUSERSNOTPOSSIBLE}"
        ${endif}
      ${endif}

    !endif

  ${else}

    ;Not running Windows NT, per-user installation not supported

    Call ${UNINSTALLER_FUNCPREFIX}MultiUser.InstallMode.AllUsers

  ${endif}

!macroend

!macro MULTIUSER_INIT
  !verbose push
  !verbose 3

  !insertmacro MULTIUSER_INIT_CHECKS "" ""

  !verbose pop
!macroend

!ifndef MULTIUSER_NOUNINSTALL

!macro MULTIUSER_UNINIT
  !verbose push
  !verbose 3

  !insertmacro MULTIUSER_INIT_CHECKS Un un.

  !verbose pop
!macroend

!endif

/*

Modern UI 2 page

*/

!ifdef MULTIUSER_MUI

!macro MULTIUSER_INSTALLMODEPAGE_INTERFACE

  !ifndef MULTIUSER_INSTALLMODEPAGE_INTERFACE
    !define MULTIUSER_INSTALLMODEPAGE_INTERFACE
    Var MultiUser.InstallModePage

    Var MultiUser.InstallModePage.Text

    Var MultiUser.InstallModePage.AllUsers
    Var MultiUser.InstallModePage.CurrentUser

    Var MultiUser.InstallModePage.ReturnValue
  !endif

!macroend

!macro MULTIUSER_PAGEDECLARATION_INSTALLMODE

  !insertmacro MUI_SET MULTIUSER_${MUI_PAGE_UNINSTALLER_PREFIX}INSTALLMODEPAGE ""
  !insertmacro MULTIUSER_INSTALLMODEPAGE_INTERFACE

  !insertmacro MUI_DEFAULT MULTIUSER_INSTALLMODEPAGE_TEXT_TOP "$(MULTIUSER_INNERTEXT_INSTALLMODE_TOP)"
  !insertmacro MUI_DEFAULT MULTIUSER_INSTALLMODEPAGE_TEXT_ALLUSERS "$(MULTIUSER_INNERTEXT_INSTALLMODE_ALLUSERS)"
  !insertmacro MUI_DEFAULT MULTIUSER_INSTALLMODEPAGE_TEXT_CURRENTUSER "$(MULTIUSER_INNERTEXT_INSTALLMODE_CURRENTUSER)"

  PageEx custom

    PageCallbacks MultiUser.InstallModePre_${MUI_UNIQUEID} MultiUser.InstallModeLeave_${MUI_UNIQUEID}

    Caption " "

  PageExEnd

  !insertmacro MULTIUSER_FUNCTION_INSTALLMODEPAGE MultiUser.InstallModePre_${MUI_UNIQUEID} MultiUser.InstallModeLeave_${MUI_UNIQUEID}

  !undef MULTIUSER_INSTALLMODEPAGE_TEXT_TOP
  !undef MULTIUSER_INSTALLMODEPAGE_TEXT_ALLUSERS
  !undef MULTIUSER_INSTALLMODEPAGE_TEXT_CURRENTUSER

!macroend

!macro MULTIUSER_PAGE_INSTALLMODE

  ;Modern UI page for install mode

  !verbose push
  !verbose 3

  !ifndef MULTIUSER_EXECUTIONLEVEL_ALLUSERS
    !error "A mixed-mode installation requires MULTIUSER_EXECUTIONLEVEL to be set to Admin, Power or Highest."
  !endif

  !insertmacro MUI_PAGE_INIT
  !insertmacro MULTIUSER_PAGEDECLARATION_INSTALLMODE

  !verbose pop

!macroend

!macro MULTIUSER_FUNCTION_INSTALLMODEPAGE PRE LEAVE

  !ifndef MULTIUSER_TEXT_NOTADMIN
    !define MULTIUSER_TEXT_NOTADMIN "The installer is currently running as an unprivileged user.  It will not be possible to install this software for all users on the system.  Cancel and re-run the installer as Administrator to install system-wide."
  !endif

  ;Page functions of Modern UI page

  Function "${PRE}"

    ${ifnot} ${IsNT}
      Abort
    ${endif}

    ${if} $MultiUser.Privileges != "Power"
      ${andif} $MultiUser.Privileges != "Admin"
	MessageBox MB_OKCANCEL "${MULTIUSER_TEXT_NOTADMIN}" /SD IDOK IDABORT nofun
	Abort

    nofun:
	Quit
    ${endif}

    !insertmacro MUI_PAGE_FUNCTION_CUSTOM PRE
    !insertmacro MUI_HEADER_TEXT_PAGE $(MULTIUSER_TEXT_INSTALLMODE_TITLE) $(MULTIUSER_TEXT_INSTALLMODE_SUBTITLE)

    nsDialogs::Create 1018
    Pop $MultiUser.InstallModePage

    ${NSD_CreateLabel} 0u 0u 300u 20u "${MULTIUSER_INSTALLMODEPAGE_TEXT_TOP}"
    Pop $MultiUser.InstallModePage.Text

    ${NSD_CreateRadioButton} 20u 50u 280u 10u "${MULTIUSER_INSTALLMODEPAGE_TEXT_ALLUSERS}"
    Pop $MultiUser.InstallModePage.AllUsers

    ${NSD_CreateRadioButton} 20u 70u 280u 10u "${MULTIUSER_INSTALLMODEPAGE_TEXT_CURRENTUSER}"
    Pop $MultiUser.InstallModePage.CurrentUser

    ${if} $MultiUser.InstallMode == "AllUsers"
      SendMessage $MultiUser.InstallModePage.AllUsers ${BM_SETCHECK} ${BST_CHECKED} 0
    ${else}
      SendMessage $MultiUser.InstallModePage.CurrentUser ${BM_SETCHECK} ${BST_CHECKED} 0
    ${endif}

    !insertmacro MUI_PAGE_FUNCTION_CUSTOM SHOW
    nsDialogs::Show

  FunctionEnd

  Function "${LEAVE}"
     SendMessage $MultiUser.InstallModePage.AllUsers ${BM_GETCHECK} 0 0 $MultiUser.InstallModePage.ReturnValue

     ${if} $MultiUser.InstallModePage.ReturnValue = ${BST_CHECKED}
        Call MultiUser.InstallMode.AllUsers
     ${else}
        Call MultiUser.InstallMode.CurrentUser
     ${endif}

    !insertmacro MUI_PAGE_FUNCTION_CUSTOM LEAVE
  FunctionEnd

!macroend

!endif

!verbose pop
!endif
