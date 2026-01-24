const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('deepframe', {
    initialize: () => ipcRenderer.invoke('deepframe:initialize'),
    start: (config) => ipcRenderer.invoke('deepframe:start', config),
    stop: () => ipcRenderer.invoke('deepframe:stop'),
    getStats: () => ipcRenderer.invoke('deepframe:getStats'),
    isRunning: () => ipcRenderer.invoke('deepframe:isRunning'),
    getOpenWindows: () => ipcRenderer.invoke('deepframe:getOpenWindows'),
    setTargetWindow: (hwnd) => ipcRenderer.invoke('deepframe:setTargetWindow', hwnd),
    setShowStats: (show) => ipcRenderer.invoke('deepframe:setShowStats', show),
    setMode: (mode) => ipcRenderer.invoke('deepframe:setMode', mode),
});

contextBridge.exposeInMainWorld('windowControls', {
    minimize: () => ipcRenderer.send('window-minimize'),
    maximize: () => ipcRenderer.send('window-maximize'),
    close: () => ipcRenderer.send('window-close'),
});

console.log('Preload script loaded successfully');
