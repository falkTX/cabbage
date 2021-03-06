# Cabbage installer and uninstaller script
# Rory Walsh Copright, 2013

!define APPNAME "Cabbage64"
!define COMPANYNAME "Cabbage Audio"
!define DESCRIPTION "Audio plugin and standalone development toolkit"
RequestExecutionLevel admin ;Require admin rights on NT6+ (When UAC is turned on)
InstallDir "$PROGRAMFILES\${COMPANYNAME}\${APPNAME}"

# This will be in the installer/uninstaller's title bar
Name "${COMPANYNAME} - ${APPNAME}"
Icon "logo.ico"
outFile "Cabbage64Installer.exe"
 
!include LogicLib.nsh
; Include LogicLibrary
!include "LogicLib.nsh"
!include "EnvVarUpdate.nsh"

; Modern interface settings
!include "MUI2.nsh"
!define MUI_LANGUAGE
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Welcome to the Cabbage 64-bit installer"
!define MUI_WELCOMEPAGE_TEXT "This Wizard will install the Cabbage audio instrument development environment to your system. Note that this is alpha softare and is for testing purposes only. This software and can only used in production at the users own risk.$\r$\n$\r$\n$\r$\nCabbage Audio accepts no reponsibility for any weirdness.."


# Just three pages - license agreement, install location, and installation
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\GNUGeneralPublicLicense.txt"
!insertmacro MUI_PAGE_DIRECTORY 
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
  
;--------------------------------
;Language
 
  !insertmacro MUI_LANGUAGE "English"
 
!macro VerifyUserIsAdmin
UserInfo::GetAccountType
pop $0
${If} $0 != "admin" ;Require admin rights on NT4+
        messageBox mb_iconstop "Administrator rights required!"
        setErrorLevel 740 ;ERROR_ELEVATION_REQUIRED
        quit
${EndIf}
!macroend
 
function .onInit
	setShellVarContext all
	!insertmacro VerifyUserIsAdmin
functionEnd
 
section "install"
	# Files for the install directory - to build the installer, these should be in the same directory as the install script (this file)
	setOutPath $INSTDIR
	# Files added here should be removed by the uninstaller (see section "uninstall")
	File "build\Cabbage64.exe"
	File "logo.ico"
	File /r "..\..\Examples"
	File /r "..\..\Docs"
	File /r "..\..\..\CsoundDocs"
	# File /r "..\..\..\CsoundLibs\CsoundPlugins"
	File "build\CabbagePluginSynth.dat"
	File "build\CabbagePluginEffect.dat"
	File "build\opcodes.txt"
           
	File "..\..\..\csound64\csound6\debug\cellular.dll"
	File "..\..\..\csound64\csound6\debug\cs_date.dll"
	File "..\..\..\csound64\csound6\debug\csladspa.dll"
	File "..\..\..\csound64\csound6\debug\csnd6.dll"
	File "..\..\..\csound64\csound6\debug\csound64.dll"
	File "..\..\..\csound64\csound6\debug\doppler.dll"
	File "..\..\..\csound64\csound6\debug\fareygen.dll"
	File "..\..\..\csound64\csound6\debug\fractalnoise.dll"
	File "..\..\..\csound64\csound6\debug\ipmidi.dll"
	File "..\..\..\csound64\csound6\debug\libcsnd6.dll.a"
	File "..\..\..\csound64\csound6\debug\libportaudio-2.dll"
	File "..\..\..\csound64\csound6\debug\libsndfile-1.dll"
	File "..\..\..\csound64\csound6\debug\mixer.dll"
	#File "..\..\..\csound64\csound6\debug\osc.dll"
	File "..\..\..\csound64\csound6\debug\platerev.dll"
	#File "..\..\..\csound64\csound6\debug\py.dll"
	File "..\..\..\csound64\csound6\debug\rtpa.dll"
	File "..\..\..\csound64\csound6\debug\rtwinmm.dll"
	File "..\..\..\csound64\csound6\debug\scansyn.dll"
	File "..\..\..\csound64\csound6\debug\serial.dll"
	File "..\..\..\csound64\csound6\debug\signalflowgraph.dll"
	File "..\..\..\csound64\csound6\debug\stdutil.dll"
	File "..\..\..\csound64\csound6\debug\system_call.dll"
	File "..\..\..\MingwLibs64\libwinpthread-1.dll"
	File "..\..\..\MingwLibs64\libgomp-1.dll"
	File "..\..\..\MingwLibs64\libgcc_s_dw2-1.dll"
	File "..\..\..\MingwLibs64\libstdc++-6.dll"
	File "..\..\..\MingwLibs64\msvcr110.dll"
	File "..\..\..\MingwLibs64\libgcc_s_seh-1.dll"
 
	# Uninstaller - See function un.onInit and section "uninstall" for configuration
	writeUninstaller "$INSTDIR\Uninstall-Cabbage64.exe"
 
	# Start Menu
	createDirectory "$SMPROGRAMS\${COMPANYNAME}"
	createShortCut "$SMPROGRAMS\${COMPANYNAME}\Uninstall-Cabbage64.lnk" "$INSTDIR\Uninstall-Cabbage64.exe"
	createShortCut "$SMPROGRAMS\${COMPANYNAME}\${APPNAME}.lnk" "$INSTDIR\Cabbage.exe" "" "$INSTDIR\logo.ico"


	${EnvVarUpdate} $0 "PATH" "P" "HKLM" "$INSTDIR"  ;Prepend path so we don't confuse a previously installed Csound 
	${EnvVarUpdate} $0 "CABBAGE_OPCODE_PATH" "P" "HKLM" "$INSTDIR"  ;Prepend path so we don't confuse a previously installed Csound 
 
sectionEnd
 
# Uninstaller
   
function un.onInit
	SetShellVarContext all
 
	#Verify the uninstaller - last chance to back out
	MessageBox MB_OKCANCEL "Permanantly remove ${APPNAME}?" IDOK next
		Abort
	next:
	!insertmacro VerifyUserIsAdmin
functionEnd
 
section "uninstall"
 
	# Remove Start Menu launcher
	delete "$SMPROGRAMS\${COMPANYNAME}\Cabbage64.lnk"
	delete "$SMPROGRAMS\${COMPANYNAME}\Uninstall-Cabbage64.lnk"
 
	# Remove files
	delete $INSTDIR\cellular.dll
	delete $INSTDIR\cs_date.dll
	delete $INSTDIR\csladspa.dll
	delete $INSTDIR\csnd6.dll
	delete $INSTDIR\csound64.dll
	delete $INSTDIR\doppler.dll
	delete $INSTDIR\fareygen.dll
	delete $INSTDIR\fractalnoise.dll
	delete $INSTDIR\ipmidi.dll
	delete $INSTDIR\libcsnd6.dll.a
	delete $INSTDIR\libportaudio-2.dll
	delete $INSTDIR\libsndfile-1.dll
	delete $INSTDIR\mixer.dll
	delete $INSTDIR\osc.dll
	delete $INSTDIR\platerev.dll
	delete $INSTDIR\py.dll
	delete $INSTDIR\rtpa.dll
	delete $INSTDIR\rtwinmm.dll
	delete $INSTDIR\scansyn.dll
	delete $INSTDIR\serial.dll
	delete $INSTDIR\signalflowgraph.dll
	delete $INSTDIR\stdutil.dll
	delete $INSTDIR\system_call.dll
	delete $INSTDIR\libwinpthread-1.dll
	delete $INSTDIR\libgomp-1.dll
	delete $INSTDIR\libgcc_s_dw2-1.dll
	delete $INSTDIR\libstdc++-6.dll
	delete $INSTDIR\msvcr110.dll
	delete $INSTDIR\libgcc_s_seh-1.dll
	delete $INSTDIR\Cabbage.exe
	delete $INSTDIR\logo.ico
	delete $INSTDIR\CabbagePluginEffect.dat
	delete $INSTDIR\CabbagePluginSynth.dat
	delete $INSTDIR\opcodes.txt

	# Always delete uninstaller as the last action
	delete $INSTDIR\uninstall.exe
 	rmDir /r $INSTDIR\CsoundDocs
 
 	rmDir /r $INSTDIR\Examples
 	rmDir /r $INSTDIR\Docs
 	# rmDir /r $INSTDIR\CsoundPlugins

 	${un.EnvVarUpdate} $0 "PATH" "R" "HKLM" "$INSTDIR"  ;remove


	# Try to remove the Start Menu folder - this will only happen if it is empty
	rmDir "$SMPROGRAMS\${COMPANYNAME}"

	# Try to remove the install directory - this will only happen if it is empty
	rmDir $INSTDIR


sectionEnd