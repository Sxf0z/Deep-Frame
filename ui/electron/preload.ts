
import { contextBridge, ipcRenderer } from 'electron';


contextBridge.exposeInMainWorld('deepframe', {
    initialize: () => ipcRenderer.invoke('deepframe:initialize'),
    start: (config: any) => ipcRenderer.invoke('deepframe:start', config),
    stop: () => ipcRenderer.invoke('deepframe:stop'),
    getStats: () => ipcRenderer.invoke('deepframe:getStats'),
    isRunning: () => ipcRenderer.invoke('deepframe:isRunning'),
});


contextBridge.exposeInMainWorld('windowControls', {
    minimize: () => ipcRenderer.send('window-minimize'),
    maximize: () => ipcRenderer.send('window-maximize'),
    close: () => ipcRenderer.send('window-close'),
});