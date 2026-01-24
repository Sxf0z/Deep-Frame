// Test script to load and test the native DeepFrame module
const path = require('path');
const fs = require('fs');

console.log('Node.js version:', process.version);
console.log('Node ABI version:', process.versions.modules);

const nativePath = path.join(__dirname, '../core/napi/build/Release/deepframe_native.node');
console.log('Looking for native module at:', nativePath);
console.log('File exists:', fs.existsSync(nativePath));

try {
    const native = require(nativePath);
    console.log('✓ Native module loaded successfully!');
    console.log('Exports:', Object.keys(native));

    // Try to instantiate DeepFrame
    if (native.DeepFrame) {
        console.log('DeepFrame class found, attempting to create instance...');
        const df = new native.DeepFrame();
        console.log('✓ DeepFrame instance created!');

        // Try to initialize
        const initResult = df.initialize();
        console.log('Initialize result:', initResult);

        if (initResult) {
            console.log('✓ DeepFrame initialized successfully!');
            df.stop();
        }
    }
} catch (err) {
    console.error('✗ Failed to load native module:', err.message);
    console.error('Full error:', err);
}
