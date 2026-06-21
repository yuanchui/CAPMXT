import { contextBridge, ipcRenderer } from 'electron';

// 暴露安全的 API 到渲染进程
contextBridge.exposeInMainWorld('electronAPI', {
  platform: process.platform,
  // 获取串口设备列表
  getSerialPorts: () => ipcRenderer.invoke('get-serial-ports'),
  // 检测 USB 设备是否存在（不要求出现 COM 口）
  checkUsbDevicePresence: (
    vendorIdHex: string,
    productIdHex: string,
    options?: { allowPowerShellFallback?: boolean; forceRefresh?: boolean }
  ) =>
    ipcRenderer.invoke('check-usb-device-presence', vendorIdHex, productIdHex, options),
  // 连接串口
  connectSerialPort: (portPath: string, baudRate: number = 115200) => 
    ipcRenderer.invoke('connect-serial-port', portPath, baudRate),
  // 断开串口
  disconnectSerialPort: (portPath: string) => 
    ipcRenderer.invoke('disconnect-serial-port', portPath),
  // 写入数据到串口
  writeSerialPort: (portPath: string, data: string | Uint8Array) => 
    ipcRenderer.invoke('write-serial-port', portPath, data),
  // 开始监听串口数据
  startSerialRead: (portPath: string) => 
    ipcRenderer.invoke('start-serial-read', portPath),
  // 停止监听串口数据
  stopSerialRead: (portPath: string) => 
    ipcRenderer.invoke('stop-serial-read', portPath),
  // 监听串口数据事件（主进程传 Buffer，渲染端转为 Uint8Array）
  onSerialData: (callback: (portPath: string, data: Uint8Array) => void) => {
    ipcRenderer.on('serial-data', (_event: Electron.IpcRendererEvent, portPath: string, data: Buffer | number[]) => {
      const bytes = Buffer.isBuffer(data) ? new Uint8Array(data) : Uint8Array.from(data);
      callback(portPath, bytes);
    });
    return () => ipcRenderer.removeAllListeners('serial-data');
  },
  // 主进程已组装的完整 Mode3 帧（Int16 × 256）
  onSerialMatrixFrame: (callback: (portPath: string, payload: {
    frameId: number;
    ts: number;
    min: number;
    max: number;
    mean: number;
    data: ArrayBuffer;
  }) => void) => {
    ipcRenderer.on('serial-matrix-frame', (_event: Electron.IpcRendererEvent, portPath: string, payload: {
      frameId: number;
      ts: number;
      min: number;
      max: number;
      mean: number;
      data: Buffer;
    }) => {
      const buf = payload?.data;
      const ab: ArrayBuffer = Buffer.isBuffer(buf)
        ? buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength) as ArrayBuffer
        : new ArrayBuffer(0);
      callback(portPath, { ...payload, data: ab });
    });
    return () => ipcRenderer.removeAllListeners('serial-matrix-frame');
  },
  flushSerialStream: (portPath: string) => ipcRenderer.invoke('flush-serial-stream', portPath),
  resumeSerialStream: (portPath: string) => ipcRenderer.invoke('resume-serial-stream', portPath),
  // 监听串口错误事件
  onSerialError: (callback: (portPath: string, error: string) => void) => {
    ipcRenderer.on('serial-error', (_event: Electron.IpcRendererEvent, portPath: string, error: string) => callback(portPath, error));
    return () => ipcRenderer.removeAllListeners('serial-error');
  },
  // 监听串口关闭事件
  onSerialClose: (callback: (portPath: string) => void) => {
    ipcRenderer.on('serial-close', (_event: Electron.IpcRendererEvent, portPath: string) => callback(portPath));
    return () => ipcRenderer.removeAllListeners('serial-close');
  },
  // 上传 xcfg 并通过 test-V1 执行写入，完成后自动备份
  writeXcfgAndBackup: (payload: { device?: string; fileName?: string; xcfgContent: string; backupName?: string }) =>
    ipcRenderer.invoke('write-xcfg-and-backup', payload),

  // 上传 xcfg 到 MCU：写入 -> NVM备份 -> 读回导出 xcfg
  writeXcfgAndExportFromMcu: (payload: { portPath: string; fileName?: string; xcfgContent: string; backupName?: string; preparedToken?: string; sourceDir?: string }) =>
    ipcRenderer.invoke('write-xcfg-and-export-from-mcu', payload),
  // 上传已处理 bin 到 MCU：传输 -> 备份
  writeBinAndBackupFromMcu: (payload: { portPath: string; fileName?: string; binBase64: string; backupName?: string }) =>
    ipcRenderer.invoke('write-bin-and-backup-from-mcu', payload),
  // 取消 xcfg/bin/enc 传输
  cancelXcfgTransfer: (payload: { portPath: string }) =>
    ipcRenderer.invoke('cancel-xcfg-transfer', payload),

  // 读取 MCU 芯片 Info Block（Family/Variant/Version 等）
  readMcuChipInfo: (payload?: { portPath?: string }) =>
    ipcRenderer.invoke('read-mcu-chip-info', payload || {}),

  // 从已连接设备读取完整 xcfg（format 3）
  readMcuXcfgFromDevice: (payload?: { portPath?: string }) =>
    ipcRenderer.invoke('read-mcu-xcfg-from-device', payload || {}),

  // 流式上传 .enc 固件（MCU 边收边写 Bootloader I2C）
  flashEncFromMcu: (payload: { portPath: string; encFilePath: string; fileName?: string; bootloaderAddr?: number; skipEnterBootloader?: boolean }) =>
    ipcRenderer.invoke('flash-enc-from-mcu', payload),

  // 预处理 xcfg：转换二进制并附加 CRC16，保存到指定目录
  prepareXcfgBinary: (payload: { fileName?: string; xcfgContent: string; backupName?: string; outputDir?: string }) =>
    ipcRenderer.invoke('prepare-xcfg-binary', payload),

  // 导出 prepared.bin 反解析文本
  exportPreparedBinaryTxt: (payload: { filePath?: string; binBase64?: string; preparedToken?: string; outputPath?: string }) =>
    ipcRenderer.invoke('export-prepared-binary-txt', payload),

  // 导出 xcfg 文本到本地文件
  saveXcfgContentDialog: (payload: { content: string; defaultName?: string; defaultDir?: string }) =>
    ipcRenderer.invoke('save-xcfg-content-dialog', payload),

  // 打开系统对话框选择 xcfg（返回路径+内容）
  selectXcfgFile: () => ipcRenderer.invoke('select-xcfg-file'),
  // 打开系统对话框选择 xcfg/bin
  selectConfigFile: () => ipcRenderer.invoke('select-config-file'),
  // 获取上次打开的 xcfg 目录（用于弹窗展示/默认路径）
  getLastXcfgDir: () => ipcRenderer.invoke('get-last-xcfg-dir'),
  // 获取运行时窗口配置显示信息
  getRuntimeWindowConfig: () => ipcRenderer.invoke('get-runtime-window-config'),
  // 获取应用编译模式（user/debug）
  getAppMode: () => ipcRenderer.invoke('get-app-mode'),
  // 用户数据目录等（用于导出配置时标明本地缓存物理路径）
  getAppPaths: () => ipcRenderer.invoke('get-app-paths'),
  appendSerialLog: (payload: { text: string }) => ipcRenderer.invoke('append-serial-log', payload),
  showSerialLogInExplorer: () => ipcRenderer.invoke('show-serial-log-in-explorer'),
  openDetachedModalWindow: (payload: { modalId: string; modalKey?: string; row?: number; col?: number; freqType?: string }) =>
    ipcRenderer.invoke('open-detached-modal-window', payload),

  // 打开 xcfg-viewer 配置查看器（独立窗口，支持加载/编辑/导出 xcfg 与 MD 元数据）
  openXcfgViewerWindow: (payload?: { page?: string; xcfgContent?: string; fileName?: string }) =>
    ipcRenderer.invoke('open-xcfg-viewer-window', payload),

  // 监听 xcfg 传输进度
  onXcfgTransferProgress: (callback: (progress: {
    phase: string;
    portPath?: string;
    fileName?: string;
    current?: number;
    total?: number;
    percent?: number;
    message?: string;
  }) => void) => {
    ipcRenderer.on('xcfg-transfer-progress', (_event: Electron.IpcRendererEvent, progress: any) => callback(progress));
    return () => ipcRenderer.removeAllListeners('xcfg-transfer-progress');
  },
  onSerialBridgeLog: (callback: (portPath: string, direction: 'tx' | 'rx' | 'info', text: string) => void) => {
    ipcRenderer.on('serial-bridge-log', (_event: Electron.IpcRendererEvent, portPath: string, direction: 'tx' | 'rx' | 'info', text: string) => {
      callback(portPath, direction, text);
    });
    return () => ipcRenderer.removeAllListeners('serial-bridge-log');
  },
});
