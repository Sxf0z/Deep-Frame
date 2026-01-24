// Electron main process - uses a Node.js child process for native module compatibility

// RELAUNCHER: If we are in Node mode (ELECTRON_RUN_AS_NODE), relaunch as real Electron
if (process.env.ELECTRON_RUN_AS_NODE) {
    const { spawn } = require('child_process');
    const env = { ...process.env };
    delete env.ELECTRON_RUN_AS_NODE;
    spawn(process.execPath, [__filename], { env, stdio: 'inherit' });
    process.exit(0);
}

const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('node:path');
const { fork } = require('child_process');

let win;
let nativeWorker = null;
let pendingRequests = new Map();
let requestId = 0;

function startNativeWorker() {
    nativeWorker = fork(path.join(__dirname, 'native-bridge.js'), [], {
        execPath: 'node', // Use system Node.js, not Electron
        stdio: ['pipe', 'pipe', 'pipe', 'ipc']
    });

    nativeWorker.on('message', (msg) => {
        if (msg.type === 'ready') {
            console.log('Native worker ready');
        } else if (msg.type === 'result') {
            const resolver = pendingRequests.get(msg.id);
            if (resolver) {
                pendingRequests.delete(msg.id);
                if (msg.error) resolver.reject(new Error(msg.error));
                else resolver.resolve(msg.result);
            }
        } else if (msg.type === 'error') {
            console.error('Native worker error:', msg.error);
        }
    });

    nativeWorker.on('error', (err) => console.error('Worker error:', err));
    nativeWorker.on('exit', (code) => console.log('Worker exited:', code));
}

function callNative(action, params = {}) {
    return new Promise((resolve, reject) => {
        if (!nativeWorker) {
            reject(new Error('Worker not started'));
            return;
        }
        const id = ++requestId;
        pendingRequests.set(id, { resolve, reject });
        nativeWorker.send({ id, action, ...params });
    });
}

// IPC handlers
ipcMain.handle('deepframe:initialize', async () => {
    try {
        return await callNative('initialize');
    } catch (error) {
        return { success: false, error: String(error) };
    }
});

ipcMain.handle('deepframe:start', async (event, config) => {
    try {
        return await callNative('start', { config });
    } catch (error) {
        return { success: false, error: String(error) };
    }
});

ipcMain.handle('deepframe:stop', async () => {
    try {
        return await callNative('stop');
    } catch (error) {
        return { success: true };
    }
});

ipcMain.handle('deepframe:getStats', async () => {
    try {
        return await callNative('getStats');
    } catch (error) {
        return null;
    }
});

ipcMain.handle('deepframe:isRunning', async () => {
    try {
        return await callNative('isRunning');
    } catch (error) {
        return false;
    }
});

ipcMain.handle('deepframe:getOpenWindows', async () => {
    try {
        return await callNative('getOpenWindows');
    } catch (error) {
        return [];
    }
});

ipcMain.handle('deepframe:setTargetWindow', async (event, hwnd) => {
    try {
        return await callNative('setTargetWindow', { hwnd });
    } catch (error) {
        return false;
    }
});

ipcMain.handle('deepframe:setShowStats', async (event, show) => {
    try {
        return await callNative('setShowStats', { show });
    } catch (error) {
        return false;
    }
});

ipcMain.handle('deepframe:setMode', async (event, mode) => {
    try {
        return await callNative('setMode', { mode });
    } catch (error) {
        return false;
    }
});

ipcMain.on('window-minimize', () => win?.minimize());
ipcMain.on('window-maximize', () => {
    if (win?.isMaximized()) win.unmaximize();
    else win?.maximize();
});
ipcMain.on('window-close', () => win?.close());

function createWindow() {
    win = new BrowserWindow({
        webPreferences: {
            preload: path.join(__dirname, 'preload.js'),
            contextIsolation: true,
            nodeIntegration: false,
        },
        width: 1100,
        height: 750,
        frame: false,
        backgroundColor: '#0f172a',
        show: false,
    });

    win.setMenu(null);

    const devServerUrl = process.env.VITE_DEV_SERVER_URL;
    if (devServerUrl) {
        win.loadURL(devServerUrl);
    } else {
        win.loadFile(path.join(__dirname, 'dist/index.html'));
    }

    win.once('ready-to-show', () => {
        win.show();
    });
}

app.whenReady().then(() => {
    startNativeWorker();
    createWindow();
});

app.on('window-all-closed', () => {
    if (nativeWorker) {
        callNative('stop').catch(() => { });
        nativeWorker.kill();
    }
    if (process.platform !== 'darwin') app.quit();
});
