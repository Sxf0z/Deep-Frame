
const electron = require('electron');
const { app, BrowserWindow, ipcMain } = electron;
const path = require('node:path');

process.env.DIST = path.join(__dirname, '../dist');
process.env.VITE_PUBLIC = app.isPackaged
    ? process.env.DIST
    : path.join(process.env.DIST, '../public');

let win;
let deepframeInstance = null;

const VITE_DEV_SERVER_URL = process.env['VITE_DEV_SERVER_URL'];


function loadNativeModule() {
    try {
        const nativePath = path.join(__dirname, '../../backend/napi/build/Release/deepframe_native.node');
        const native = require(nativePath);
        return native;
    } catch (error) {
        console.error('Failed to load native module:', error.message);
        return null;
    }
}


ipcMain.handle('deepframe:initialize', async () => {
    try {
        const native = loadNativeModule();
        if (!native) {
            return { success: false, error: 'Native module not found' };
        }

        deepframeInstance = new native.DeepFrame();
        const result = deepframeInstance.initialize();
        return { success: result };
    } catch (error) {
        return { success: false, error: String(error) };
    }
});

ipcMain.handle('deepframe:start', async (event, config) => {
    if (!deepframeInstance) {
        return { success: false, error: 'Not initialized' };
    }

    try {
        const result = deepframeInstance.start(config || {});
        return { success: result };
    } catch (error) {
        return { success: false, error: String(error) };
    }
});

ipcMain.handle('deepframe:stop', async () => {
    if (!deepframeInstance) {
        return { success: false };
    }

    try {
        deepframeInstance.stop();
        return { success: true };
    } catch (error) {
        return { success: false, error: String(error) };
    }
});

ipcMain.handle('deepframe:getStats', async () => {
    if (!deepframeInstance) {
        return null;
    }

    try {
        return deepframeInstance.getStats();
    } catch (error) {
        return null;
    }
});

ipcMain.handle('deepframe:isRunning', async () => {
    if (!deepframeInstance) {
        return false;
    }
    return deepframeInstance.isRunning();
});


ipcMain.on('window-minimize', () => {
    win?.minimize();
});

ipcMain.on('window-maximize', () => {
    if (win?.isMaximized()) {
        win.unmaximize();
    } else {
        win?.maximize();
    }
});

ipcMain.on('window-close', () => {
    win?.close();
});

function createWindow() {
    win = new BrowserWindow({
        icon: path.join(process.env.VITE_PUBLIC, 'electron-vite.svg'),
        webPreferences: {
            preload: path.join(__dirname, 'preload.js'),
            contextIsolation: true,
            nodeIntegration: false,
        },
        width: 1200,
        height: 800,
        titleBarStyle: 'hidden',
        backgroundColor: '#0A192F',
        show: false,
    });

    win.setMenu(null);

    win.webContents.on('did-finish-load', () => {
        win?.webContents.send('main-process-message', new Date().toLocaleString());
    });

    if (VITE_DEV_SERVER_URL) {
        win.loadURL(VITE_DEV_SERVER_URL);
    } else {
        win.loadFile(path.join(process.env.DIST, 'index.html'));
    }

    win.once('ready-to-show', () => {
        win?.show();
    });
}

app.on('window-all-closed', () => {
    
    if (deepframeInstance && deepframeInstance.isRunning()) {
        deepframeInstance.stop();
    }

    if (process.platform !== 'darwin') {
        app.quit();
    }
});

app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
        createWindow();
    }
});

app.whenReady().then(createWindow);