!macro XZ_ARCHIVE_GENERATED_RESOURCE NAME
  IfFileExists "$INSTDIR\resources\${NAME}\*.*" 0 xz_${NAME}_done
  IfFileExists "$INSTDIR\resources\runtime\manifest.json" 0 xz_${NAME}_unknown
  GetFullPathName $R0 "$INSTDIR\resources\${NAME}"
  StrCmp $R0 "" xz_${NAME}_failed
  ClearErrors
  CreateDirectory "$INSTDIR\resource-backups"
  IfErrors xz_${NAME}_failed
  GetFullPathName $R1 "$INSTDIR\resource-backups"
  StrCmp $R1 "" xz_${NAME}_failed
  StrCpy $R2 1
xz_${NAME}_find:
  IfFileExists "$R1\${NAME}-$R2" 0 xz_${NAME}_move
  IntOp $R2 $R2 + 1
  Goto xz_${NAME}_find
xz_${NAME}_move:
  ClearErrors
  Rename "$R0" "$R1\${NAME}-$R2"
  IfErrors xz_${NAME}_failed xz_${NAME}_done
xz_${NAME}_unknown:
  Abort "Existing resource files could not be identified as XZ Mods files. Nothing was removed."
xz_${NAME}_failed:
  Abort "Close XZ Mods and retry. Its previous bundled files could not be backed up."
xz_${NAME}_done:
!macroend

!macro NSIS_HOOK_PREINSTALL
  Push $R0
  Push $R1
  Push $R2
  !insertmacro XZ_ARCHIVE_GENERATED_RESOURCE source
  !insertmacro XZ_ARCHIVE_GENERATED_RESOURCE ota-release
  Pop $R2
  Pop $R1
  Pop $R0
!macroend
