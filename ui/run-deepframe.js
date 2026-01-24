// Simple Node.js runner for DeepFrame - bypasses Electron for testing
const path = require('path');
const readline = require('readline');

const nativePath = path.join(__dirname, '../core/napi/build/Release/deepframe_native.node');

console.log('═══════════════════════════════════════════');
console.log('       DeepFrame Native Module Tester');
console.log('═══════════════════════════════════════════\n');

let native, df;

try {
    native = require(nativePath);
    console.log('✓ Native module loaded successfully!\n');
} catch (err) {
    console.error('✗ Failed to load native module:', err.message);
    process.exit(1);
}

// Create DeepFrame instance
df = new native.DeepFrame();

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

function showMenu() {
    console.log('\n─────────────────────────────────────────');
    console.log('Commands:');
    console.log('  1. Initialize DeepFrame');
    console.log('  2. List open windows');
    console.log('  3. Set target window');
    console.log('  4. Start frame generation');
    console.log('  5. Stop frame generation');
    console.log('  6. Get stats');
    console.log('  7. Toggle stats overlay');
    console.log('  8. Set mode (fast/balanced/quality)');
    console.log('  0. Exit');
    console.log('─────────────────────────────────────────');
    rl.question('\nEnter command: ', handleCommand);
}

let windows = [];

async function handleCommand(cmd) {
    console.log('');

    switch (cmd.trim()) {
        case '1':
            console.log('Initializing DeepFrame...');
            const initResult = df.initialize();
            console.log(initResult ? '✓ Initialized successfully!' : '✗ Failed to initialize');
            break;

        case '2':
            windows = df.getOpenWindows();
            console.log(`Found ${windows.length} windows:\n`);
            windows.forEach((w, i) => {
                console.log(`  ${i + 1}. [${w.hwnd}] ${w.title}`);
            });
            break;

        case '3':
            if (windows.length === 0) {
                console.log('Please list windows first (option 2)');
            } else {
                rl.question('Enter window number: ', (num) => {
                    const idx = parseInt(num) - 1;
                    if (idx >= 0 && idx < windows.length) {
                        df.setTargetWindow(windows[idx].hwnd);
                        console.log(`✓ Target set to: ${windows[idx].title}`);
                    } else {
                        console.log('Invalid window number');
                    }
                    showMenu();
                });
                return;
            }
            break;

        case '4':
            console.log('Starting frame generation...');
            const startResult = df.start({ showStats: true });
            console.log(startResult ? '✓ Started!' : '✗ Failed to start');
            break;

        case '5':
            console.log('Stopping...');
            df.stop();
            console.log('✓ Stopped');
            break;

        case '6':
            const stats = df.getStats();
            console.log('Current Stats:');
            console.log(`  Capture FPS: ${stats.captureFps?.toFixed(1) || 'N/A'}`);
            console.log(`  Present FPS: ${stats.presentFps?.toFixed(1) || 'N/A'}`);
            console.log(`  Inference Time: ${stats.inferenceTimeMs?.toFixed(2) || 'N/A'} ms`);
            console.log(`  Dropped Frames: ${stats.droppedFrames || 0}`);
            break;

        case '7':
            rl.question('Show stats overlay? (y/n): ', (ans) => {
                df.setShowStats(ans.toLowerCase() === 'y');
                console.log('✓ Stats overlay updated');
                showMenu();
            });
            return;

        case '8':
            rl.question('Enter mode (fast/balanced/quality): ', (mode) => {
                df.setMode(mode.toLowerCase());
                console.log(`✓ Mode set to: ${mode}`);
                showMenu();
            });
            return;

        case '0':
            console.log('Stopping DeepFrame...');
            df.stop();
            console.log('Goodbye!');
            rl.close();
            process.exit(0);
            return;

        default:
            console.log('Unknown command');
    }

    showMenu();
}

// Handle Ctrl+C gracefully
rl.on('close', () => {
    df.stop();
    process.exit(0);
});

showMenu();
