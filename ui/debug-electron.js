const m = require('module');
const electron = m._load('electron', null, true);
console.log('DEBUG: typeof electron:', typeof electron);
if (typeof electron === 'object') {
    console.log('DEBUG: ipcMain exists:', !!electron.ipcMain);
}
process.exit(0);
