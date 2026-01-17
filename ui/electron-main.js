const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('node:path');


const DIST = path.join(__dirname, 'dist');

let win;
let deepframeInstance = null;
let nativeAvailable = false;


function loadNativeModule() {
    if (nativeAvailable && deepframeInstance) {
        return { DeepFrame: function () { return deepframeInstance; } };
    }

    try {
        const nativePath = path.join(__dirname, '../core/napi/build/Release/deepframe_native.node');
        console.log('Loading native module from:', nativePath);


        const fs = require('fs');
        if (!fs.existsSync(nativePath)) {
            console.error('Native module file not found at:', nativePath);
            return null;
        }


        const native = require(nativePath);
        console.log('Native module loaded successfully');
        nativeAvailable = true;
        return native;
    } catch (error) {
        console.error('Failed to load native module:', error);
        console.error('Stack:', error.stack);
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
        console.log('DeepFrame initialized:', result);
        return { success: result };
    } catch (error) {
        console.error('Initialize error:', error);
        return { success: false, error: String(error) };
    }
});

ipcMain.handle('deepframe:start', async (event, config) => {
    if (!deepframeInstance) {
        return { success: false, error: 'Not initialized' };
    }

    try {
        const result = deepframeInstance.start(config || {});
        console.log('DeepFrame started:', result);
        return { success: result };
    } catch (error) {
        console.error('Start error:', error);
        return { success: false, error: String(error) };
    }
});

ipcMain.handle('deepframe:stop', async () => {
    if (!deepframeInstance) {
        return { success: false };
    }

    try {
        deepframeInstance.stop();
        console.log('DeepFrame stopped');
        return { success: true };
    } catch (error) {
        console.error('Stop error:', error);
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
    try {
        return deepframeInstance.isRunning();
    } catch (error) {
        return false;
    }
});


ipcMain.handle('deepframe:getOpenWindows', async () => {
    if (!deepframeInstance) {
        return [];
    }
    try {
        return deepframeInstance.getOpenWindows();
    } catch (error) {
        console.error('getOpenWindows error:', error);
        return [];
    }
});


ipcMain.handle('deepframe:setTargetWindow', async (event, hwnd) => {
    if (!deepframeInstance) {
        return false;
    }
    try {
        return deepframeInstance.setTargetWindow(hwnd);
    } catch (error) {
        console.error('setTargetWindow error:', error);
        return false;
    }
});


ipcMain.handle('deepframe:setShowStats', async (event, show) => {
    if (!deepframeInstance) {
        return false;
    }
    try {
        return deepframeInstance.setShowStats(show);
    } catch (error) {
        console.error('setShowStats error:', error);
        return false;
    }
});


ipcMain.handle('deepframe:setMode', async (event, mode) => {
    if (!deepframeInstance) {
        return false;
    }
    try {
        return deepframeInstance.setMode(mode);
    } catch (error) {
        console.error('setMode error:', error);
        return false;
    }
});


ipcMain.on('window-minimize', () => win?.minimize());
ipcMain.on('window-maximize', () => {
    if (win?.isMaximized()) {
        win.unmaximize();
    } else {
        win?.maximize();
    }
});
ipcMain.on('window-close', () => win?.close());

function createWindow() {
    console.log('Creating window...');
    win = new BrowserWindow({
        webPreferences: {
            preload: path.join(__dirname, 'preload.js'),
            contextIsolation: true,
            nodeIntegration: false,
        },
        width: 1000,
        height: 700,
        frame: false,
        transparent: false,
        backgroundColor: '#0f172a',
        hasShadow: true,
        resizable: true,
        minWidth: 900,
        minHeight: 600,
        show: false,
    });

    win.setMenu(null);


    if (process.env.VITE_DEV_SERVER_URL) {
        console.log('Loading Dev Server:', process.env.VITE_DEV_SERVER_URL);
        win.loadURL(process.env.VITE_DEV_SERVER_URL);
    } else {
        win.loadFile(path.join(DIST, 'index.html'));
    }

    win.once('ready-to-show', () => {
        win.show();
    });





}

app.whenReady().then(createWindow);

app.on('window-all-closed', () => {

    if (deepframeInstance) {
        try {
            deepframeInstance.stop();
        } catch (e) {

        }
    }
    if (process.platform !== 'darwin') app.quit();
});