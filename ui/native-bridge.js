// Bridge worker - runs in a separate Node.js process to handle the native module
const path = require('path');

const nativePath = path.join(__dirname, '../core/napi/build/Release/deepframe_native.node');
let native, df;

try {
    native = require(nativePath);
    df = new native.DeepFrame();
    process.send({ type: 'ready' });
} catch (err) {
    process.send({ type: 'error', error: err.message });
    process.exit(1);
}

process.on('message', (msg) => {
    try {
        let result;
        switch (msg.action) {
            case 'initialize':
                result = { success: df.initialize() };
                break;
            case 'start':
                result = { success: df.start(msg.config || { showStats: true }) };
                break;
            case 'stop':
                df.stop();
                result = { success: true };
                break;
            case 'getStats':
                result = df.getStats();
                break;
            case 'isRunning':
                result = df.isRunning();
                break;
            case 'getOpenWindows':
                result = df.getOpenWindows();
                break;
            case 'setTargetWindow':
                result = df.setTargetWindow(msg.hwnd);
                break;
            case 'setShowStats':
                result = df.setShowStats(msg.show);
                break;
            case 'setMode':
                result = df.setMode(msg.mode);
                break;
            default:
                result = { error: 'Unknown action' };
        }
        process.send({ type: 'result', id: msg.id, result });
    } catch (err) {
        process.send({ type: 'result', id: msg.id, error: err.message });
    }
});

process.on('disconnect', () => {
    if (df) df.stop();
    process.exit(0);
});
