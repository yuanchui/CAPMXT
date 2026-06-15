; 全新安装（注册表无 InstallLocation）且未传 /D= 路径时，若 D: 为本地固定磁盘，则默认安装到 D 盘。
; 当前用户：D:\Programs\<APP>；所有用户：D:\Program Files [\(x86)\]…\<APP>（与官方 per-machine 规则一致）
!macro customInit
  ReadRegStr $R5 HKCU "${INSTALL_REGISTRY_KEY}" InstallLocation
  ReadRegStr $R6 HKLM "${INSTALL_REGISTRY_KEY}" InstallLocation
  ${If} $R5 == ""
  ${AndIf} $R6 == ""
    ${StdUtils.GetParameter} $R0 "D" ""
    ${If} $R0 == ""
      System::Call 'kernel32::GetDriveType(w "D:\") i.r0'
      StrCpy $R8 $0
      ${If} $R8 == 3
        ${If} $installMode == "all"
          !ifdef APP_64
            ${if} ${RunningX64}
              StrCpy $R7 "D:\Program Files"
            ${else}
              StrCpy $R7 "D:\Program Files (x86)"
            ${endif}
          !else
            StrCpy $R7 "D:\Program Files (x86)"
          !endif
          !ifdef MENU_FILENAME
            StrCpy $INSTDIR "$R7\${MENU_FILENAME}\${APP_FILENAME}"
          !else
            StrCpy $INSTDIR "$R7\yuanchu\${APP_FILENAME}"
          !endif
        ${Else}
          StrCpy $INSTDIR "D:\Programs\yuanchu\${APP_FILENAME}"
        ${EndIf}
      ${EndIf}
    ${EndIf}
  ${EndIf}
!macroend

; 安装程序默认用 exe 内嵌图标作为快捷方式图标；若 exe 图标未更新或系统缓存旧图标，
; 桌面/开始菜单仍可能显示 Electron 默认图标。此处用与运行时相同的 resources\icon.ico 覆盖快捷方式图标。
!macro customInstall
  ${If} ${FileExists} "$INSTDIR\resources\icon.ico"
    ${If} ${FileExists} "$newStartMenuLink"
      CreateShortCut "$newStartMenuLink" "$appExe" "" "$INSTDIR\resources\icon.ico" 0 "" "" "${APP_DESCRIPTION}"
      WinShell::SetLnkAUMI "$newStartMenuLink" "${APP_ID}"
    ${EndIf}
    ${If} ${FileExists} "$newDesktopLink"
      CreateShortCut "$newDesktopLink" "$appExe" "" "$INSTDIR\resources\icon.ico" 0 "" "" "${APP_DESCRIPTION}"
      WinShell::SetLnkAUMI "$newDesktopLink" "${APP_ID}"
    ${EndIf}
    System::Call 'Shell32::SHChangeNotify(i 0x8000000, i 0, i 0, i 0)'
  ${EndIf}
!macroend

; 安装完成后打开升级说明（若已打包进 resources）
!macro customFinish
  ${If} ${FileExists} "$INSTDIR\resources\UPGRADE_NOTES.txt"
    ExecShell "open" "$INSTDIR\resources\UPGRADE_NOTES.txt"
  ${EndIf}
!macroend
