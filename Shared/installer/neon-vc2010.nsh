!ifndef NEON_VC2010_INSTALLER
    !define NEON_VC2010_INSTALLER "../../Build/runtime-prerequisites/vcredist_x86.exe"
!endif

Function InstallNeonVC2010
    Push $0
    Push $1
    ; NSIS is x86: its system directory resolves to the 32-bit runtime even on
    ; x64/ARM64 Windows. A modern VC++ 2015-2022 runtime does not provide this DLL.
    System::Call 'kernel32::LoadLibraryW(w "$SYSDIR\msvcr100.dll") p.r0'
    ${If} $0 != 0
        System::Call 'kernel32::FreeLibrary(p r0)'
    ${Else}
        InitPluginsDir
        SetOutPath "$PLUGINSDIR"
        File /oname=vcredist_x86.exe "${NEON_VC2010_INSTALLER}"
        DetailPrint "Installing Visual C++ 2010 SP1 x86 for HD vehicle audio..."
        StrCpy $1 -1
        ClearErrors
        ExecWait '"$PLUGINSDIR\vcredist_x86.exe" /q /norestart' $1
        ${If} ${Errors}
            StrCpy $1 -1
        ${EndIf}
        ${If} $1 == 3010
            SetRebootFlag true
        ${ElseIf} $1 != 0
        ${AndIf} $1 != 1638
            ; Never report a successful Neon install with a broken audio runtime.
            MessageBox MB_OK|MB_ICONSTOP "Visual C++ 2010 SP1 x86 installation failed (code $1). Install the Microsoft x86 redistributable, then run Neon Setup again." /SD IDOK
            SetErrorLevel 1603
            Abort
        ${EndIf}
        ; 1638 can mean a newer package exists. Verify the actual x86 dependency
        ; rather than trusting an installed-products registry entry or exit code.
        System::Call 'kernel32::LoadLibraryW(w "$SYSDIR\msvcr100.dll") p.r0'
        ${If} $0 == 0
            MessageBox MB_OK|MB_ICONSTOP "The 32-bit MSVCR100.dll runtime is still unavailable. Restart Windows if requested, repair Visual C++ 2010 SP1 x86, then run Neon Setup again." /SD IDOK
            SetErrorLevel 1603
            Abort
        ${EndIf}
        System::Call 'kernel32::FreeLibrary(p r0)'
    ${EndIf}
    Pop $1
    Pop $0
FunctionEnd
