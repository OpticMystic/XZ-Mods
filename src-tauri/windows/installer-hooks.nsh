!macro NSIS_HOOK_PREINSTALL
  Push $R0
  Push $R1
  Push $R2
  IfFileExists "$INSTDIR\resources\*.*" 0 xz_resources_done
  IfFileExists "$INSTDIR\resources\runtime\manifest.json" 0 xz_resources_unknown
  GetFullPathName $R0 "$INSTDIR\resources"
  StrCmp $R0 "" xz_resources_failed
  ClearErrors
  CreateDirectory "$INSTDIR\resource-backups"
  IfErrors xz_resources_failed
  GetFullPathName $R1 "$INSTDIR\resource-backups"
  StrCmp $R1 "" xz_resources_failed
  StrCpy $R2 1
xz_resources_find:
  IfFileExists "$R1\resources-$R2" 0 xz_resources_move
  IntOp $R2 $R2 + 1
  Goto xz_resources_find
xz_resources_move:
  ClearErrors
  Rename "$R0" "$R1\resources-$R2"
  IfErrors xz_resources_failed xz_resources_done
xz_resources_unknown:
  Abort "Existing resource files could not be identified as XZ Mods files. Nothing was removed."
xz_resources_failed:
  Abort "Close XZ Mods and retry. Its previous bundled files could not be backed up."
xz_resources_done:
  Pop $R2
  Pop $R1
  Pop $R0
!macroend
