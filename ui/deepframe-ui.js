// Simple web UI for DeepFrame - runs on Node.js with a browser interface
const http = require('http');
const path = require('path');
const url = require('url');

const nativePath = path.join(__dirname, '../core/napi/build/Release/deepframe_native.node');
let native, df;

try {
    native = require(nativePath);
    df = new native.DeepFrame();
    console.log('✓ Native module loaded');
} catch (err) {
    console.error('Failed to load native module:', err.message);
    process.exit(1);
}

const PORT = 3000;

const html = `<!DOCTYPE html>
<html>
<head>
    <title>DeepFrame Tester</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: Arial, sans-serif; background: #1a1a2e; color: #eee; padding: 20px; }
        h1 { color: #0f0; margin-bottom: 20px; }
        .btn { 
            background: #16213e; border: 1px solid #0f3460; color: #fff; 
            padding: 15px 30px; margin: 5px; cursor: pointer; font-size: 16px; 
        }
        .btn:hover { background: #0f3460; }
        .btn.active { background: #0f0; color: #000; }
        #status { background: #0d0d0d; padding: 15px; margin: 20px 0; border-radius: 5px; }
        #windows { background: #0d0d0d; padding: 15px; margin: 20px 0; max-height: 300px; overflow-y: auto; }
        .window-item { padding: 10px; cursor: pointer; border-bottom: 1px solid #333; }
        .window-item:hover { background: #1a1a2e; }
        #stats { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin: 20px 0; }
        .stat { background: #16213e; padding: 15px; text-align: center; }
        .stat-value { font-size: 24px; color: #0f0; }
        .stat-label { font-size: 12px; color: #888; }
    </style>
</head>
<body>
    <h1>🎮 DeepFrame Tester</h1>
    
    <div>
        <button class="btn" onclick="init()">1. Initialize</button>
        <button class="btn" onclick="listWindows()">2. List Windows</button>
        <button class="btn" onclick="start()">3. Start</button>
        <button class="btn" onclick="stop()">4. Stop</button>
        <button class="btn" onclick="getStats()">5. Get Stats</button>
    </div>
    
    <div id="status">Status: Ready</div>
    
    <div id="stats">
        <div class="stat"><div class="stat-value" id="captureFps">-</div><div class="stat-label">Capture FPS</div></div>
        <div class="stat"><div class="stat-value" id="presentFps">-</div><div class="stat-label">Present FPS</div></div>
        <div class="stat"><div class="stat-value" id="latency">-</div><div class="stat-label">Latency (ms)</div></div>
    </div>
    
    <div id="windows"></div>
    
    <script>
        async function api(action) {
            const res = await fetch('/api?action=' + action);
            return res.json();
        }
        
        async function init() {
            const r = await api('init');
            document.getElementById('status').textContent = 'Status: ' + (r.success ? 'Initialized ✓' : 'Failed: ' + r.error);
        }
        
        async function listWindows() {
            const r = await api('windows');
            const div = document.getElementById('windows');
            div.innerHTML = '<b>Windows (click to select):</b><br>';
            r.windows.forEach(w => {
                const item = document.createElement('div');
                item.className = 'window-item';
                item.textContent = w.title;
                item.onclick = () => selectWindow(w.hwnd, w.title);
                div.appendChild(item);
            });
        }
        
        async function selectWindow(hwnd, title) {
            await fetch('/api?action=target&hwnd=' + hwnd);
            document.getElementById('status').textContent = 'Status: Targeting "' + title + '"';
        }
        
        async function start() {
            const r = await api('start');
            document.getElementById('status').textContent = 'Status: ' + (r.success ? 'Running ✓' : 'Failed');
            if (r.success) setInterval(getStats, 1000);
        }
        
        async function stop() {
            await api('stop');
            document.getElementById('status').textContent = 'Status: Stopped';
        }
        
        async function getStats() {
            const r = await api('stats');
            if (r.stats) {
                document.getElementById('captureFps').textContent = (r.stats.captureFps || 0).toFixed(1);
                document.getElementById('presentFps').textContent = (r.stats.presentFps || 0).toFixed(1);
                document.getElementById('latency').textContent = (r.stats.inferenceTimeMs || 0).toFixed(2);
            }
        }
    </script>
</body>
</html>`;

const server = http.createServer((req, res) => {
    const parsed = url.parse(req.url, true);

    if (parsed.pathname === '/api') {
        res.setHeader('Content-Type', 'application/json');
        const action = parsed.query.action;

        try {
            switch (action) {
                case 'init':
                    res.end(JSON.stringify({ success: df.initialize() }));
                    break;
                case 'windows':
                    res.end(JSON.stringify({ windows: df.getOpenWindows() }));
                    break;
                case 'target':
                    df.setTargetWindow(parseInt(parsed.query.hwnd));
                    res.end(JSON.stringify({ success: true }));
                    break;
                case 'start':
                    res.end(JSON.stringify({ success: df.start({ showStats: true }) }));
                    break;
                case 'stop':
                    df.stop();
                    res.end(JSON.stringify({ success: true }));
                    break;
                case 'stats':
                    res.end(JSON.stringify({ stats: df.getStats() }));
                    break;
                default:
                    res.end(JSON.stringify({ error: 'Unknown action' }));
            }
        } catch (err) {
            res.end(JSON.stringify({ error: err.message }));
        }
    } else {
        res.setHeader('Content-Type', 'text/html');
        res.end(html);
    }
});

server.listen(PORT, () => {
    console.log(`\n🎮 DeepFrame UI running at: http://localhost:${PORT}\n`);

    // Auto-open browser
    const { exec } = require('child_process');
    exec('start http://localhost:' + PORT);
});
