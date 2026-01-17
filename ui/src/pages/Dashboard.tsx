import React, { useState, useEffect } from 'react';
import { TitleBar } from '../components/TitleBar';
import { ScaleButton } from '../components/ScaleButton';
import { ConfigPanel, ConfigRow } from '../components/ConfigPanel';
import { ToggleSwitch } from '../components/ToggleSwitch';
import { StatusCard } from '../components/StatusCard';
import { useDeepFrame } from '../hooks/useDeepFrame';
import type { FrameGenConfig } from '../hooks/useDeepFrame';
import '../styles/components.css';

interface WindowInfo {
    hwnd: number;
    title: string;
    className: string;
}

export const Dashboard: React.FC = () => {
    
    const [perfMode, setPerfMode] = useState(false);
    const [resize, setResize] = useState(true);
    const [hdr, setHdr] = useState(false);
    const [drawFps, setDrawFps] = useState(true);
    const [legacy, setLegacy] = useState(false);
    const [scaleFactor, setScaleFactor] = useState(2);
    const [captureApi, setCaptureApi] = useState<'DXGI' | 'WGC' | 'GDI'>('DXGI');

    
    const [aiMode, setAiMode] = useState<'fast' | 'balanced' | 'quality'>('fast');

    
    const [windowList, setWindowList] = useState<WindowInfo[]>([]);
    const [targetWindow, setTargetWindow] = useState<number | null>(null);

    
    const { isElectron, isInitialized, isRunning, stats, error, start, stop } = useDeepFrame();

    
    useEffect(() => {
        const fetchWindows = async () => {
            if (isElectron && isInitialized && window.deepframe?.getOpenWindows) {
                try {
                    const windows = await window.deepframe.getOpenWindows();
                    setWindowList(windows || []);
                } catch (e) {
                    console.error('Failed to get windows:', e);
                }
            }
        };
        fetchWindows();
        
        const interval = setInterval(fetchWindows, 5000);
        return () => clearInterval(interval);
    }, [isElectron, isInitialized]);

    const handleScaleStart = async () => {
        
        if (targetWindow && window.deepframe?.setTargetWindow) {
            await window.deepframe.setTargetWindow(targetWindow);
        }
        
        if (window.deepframe?.setShowStats) {
            await window.deepframe.setShowStats(drawFps);
        }
        const config: FrameGenConfig = {
            scaleFactor,
            captureApi,
            performanceMode: perfMode,
        };
        await start(config);
    };

    const handleScaleStop = async () => {
        await stop();
    };

    return (
        <div className="dashboard-container">
            {}
            <TitleBar />

            <div className="app-body">
                {}
                <aside className="sidebar">
                    <div className="side-header">Profiles</div>
                    <div className="nav-item active">Default</div>
                    <div className="nav-item">Cyberpunk 2077</div>
                    <div className="nav-item">Elden Ring</div>
                    <div style={{ flex: 1 }}></div>
                    <div className="nav-item">
                        <span style={{ marginRight: 8 }}>⚙️</span> Settings
                    </div>
                </aside>

                <div style={{ display: 'flex', flexDirection: 'column', flex: 1 }}>
                    {}
                    <div className="header-actions">
                        <ScaleButton
                            onScaleStart={handleScaleStart}
                            onScaleStop={handleScaleStop}
                            disabled={!isInitialized && isElectron}
                            isRunning={isRunning}
                        />

                        {}
                        {isRunning && stats && (
                            <div className="status-row" style={{ display: 'flex', gap: '12px', marginLeft: '20px' }}>
                                <StatusCard label="FPS" value={stats.fps.toFixed(1)} />
                                <StatusCard label="Latency" value={stats.latencyMs.toFixed(1)} unit="ms" />
                                <StatusCard label="Frames" value={stats.framesGenerated} />
                            </div>
                        )}

                        {}
                        {error && (
                            <span style={{ color: 'var(--color-danger)', marginLeft: '20px', fontSize: '0.9em' }}>
                                {error}
                            </span>
                        )}

                        {}
                        {!isElectron && (
                            <span style={{ color: 'var(--color-text-muted)', marginLeft: '20px', fontSize: '0.9em' }}>
                                (Browser mode - backend unavailable)
                            </span>
                        )}
                    </div>

                    {}
                    <main className="main-grid">

                        {}
                        <ConfigPanel title="Target Window">
                            <ConfigRow label="Window">
                                <select
                                    value={targetWindow ?? ''}
                                    onChange={(e) => setTargetWindow(e.target.value ? Number(e.target.value) : null)}
                                >
                                    <option value="">Fullscreen</option>
                                    {windowList.map((w) => (
                                        <option key={w.hwnd} value={w.hwnd}>
                                            {w.title.length > 30 ? w.title.slice(0, 30) + '...' : w.title}
                                        </option>
                                    ))}
                                </select>
                            </ConfigRow>
                        </ConfigPanel>

                        {}
                        <ConfigPanel title="Frame Generation">
                            <ConfigRow label="AI Model">
                                <select
                                    value={aiMode}
                                    onChange={async (e) => {
                                        const mode = e.target.value as 'fast' | 'balanced' | 'quality';
                                        setAiMode(mode);
                                        if (window.deepframe?.setMode) {
                                            await window.deepframe.setMode(mode);
                                        }
                                    }}
                                >
                                    <option value="fast">RIFE (Fast - 8ms)</option>
                                    <option value="balanced">IFRNet (Balanced - 12ms)</option>
                                    <option value="quality">FILM (Quality - 20ms)</option>
                                </select>
                            </ConfigRow>
                            <ConfigRow label="Mode">
                                <select
                                    value={`X${scaleFactor}`}
                                    onChange={(e) => setScaleFactor(parseInt(e.target.value.slice(1)))}
                                >
                                    <option value="X2">X2</option>
                                    <option value="X3">X3</option>
                                    <option value="X4">X4</option>
                                </select>
                            </ConfigRow>
                            <ConfigRow label="Performance">
                                <ToggleSwitch checked={perfMode} onChange={setPerfMode} />
                            </ConfigRow>
                        </ConfigPanel>

                        {}
                        <ConfigPanel title="Scaling">
                            <ConfigRow label="Type">
                                <select defaultValue="Off">
                                    <option>Off</option>
                                    <option>Integer</option>
                                    <option>FSR</option>
                                    <option>Bicubic</option>
                                </select>
                            </ConfigRow>
                            <ConfigRow label="Mode">
                                <select defaultValue="Auto">
                                    <option>Auto</option>
                                    <option>Aspect Ratio</option>
                                    <option>Fullscreen</option>
                                </select>
                            </ConfigRow>
                            <ConfigRow label="Resize before scaling">
                                <ToggleSwitch checked={resize} onChange={setResize} />
                            </ConfigRow>
                        </ConfigPanel>

                        {}
                        <ConfigPanel title="Capture">
                            <ConfigRow label="Capture API">
                                <select
                                    value={captureApi}
                                    onChange={(e) => setCaptureApi(e.target.value as 'DXGI' | 'WGC' | 'GDI')}
                                >
                                    <option value="DXGI">DXGI</option>
                                    <option value="WGC">WGC</option>
                                    <option value="GDI">GDI</option>
                                </select>
                            </ConfigRow>
                            <ConfigRow label="Legacy Mode">
                                <ToggleSwitch checked={legacy} onChange={setLegacy} />
                            </ConfigRow>
                        </ConfigPanel>

                        {}
                        <ConfigPanel title="Rendering">
                            <ConfigRow label="Sync Mode">
                                <select defaultValue="Default">
                                    <option>Default</option>
                                    <option>On</option>
                                    <option>Off</option>
                                </select>
                            </ConfigRow>
                            <ConfigRow label="Max Latency">
                                <select defaultValue="2">
                                    <option>1</option>
                                    <option>2</option>
                                    <option>3</option>
                                </select>
                            </ConfigRow>
                            <ConfigRow label="Draw FPS">
                                <ToggleSwitch checked={drawFps} onChange={setDrawFps} />
                            </ConfigRow>
                            <ConfigRow label="HDR Support">
                                <ToggleSwitch checked={hdr} onChange={setHdr} />
                            </ConfigRow>
                        </ConfigPanel>

                    </main>
                </div>
            </div>
        </div>
    );
};