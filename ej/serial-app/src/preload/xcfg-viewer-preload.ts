import { contextBridge, ipcRenderer } from 'electron';

contextBridge.exposeInMainWorld('xcfgViewerAPI', {
  request: (payload: { path: string; method?: string; body?: unknown }) =>
    ipcRenderer.invoke('xcfg-viewer-api', payload),
  getInitialPayload: () => ipcRenderer.invoke('xcfg-viewer-get-initial'),
  clearInitialPayload: () => ipcRenderer.invoke('xcfg-viewer-clear-initial'),
  getSerialAppConnection: () => ipcRenderer.invoke('xcfg-viewer-get-connection')
});
