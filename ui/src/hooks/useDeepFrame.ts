import { useState, useEffect, useCallback } from 'react';

export interface FrameStats {
    fps: number;
    latencyMs: number;
    framesGenerated: number;
    width: number;
    height: number;
}

export interface FrameGenConfig {
    diffThreshold?: number;
    searchRadius?: number;
    temporalBlend?: number;
    scaleFactor?: number;
    captureApi?: 'DXGI' | 'WGC' | 'GDI';
    performanceMode?: boolean;
}

interface DeepFrameResult {
    success: boolean;
    error?: string;
}


declare global {
    interface Window {
        deepframe?: {
            initialize: () => Promise<DeepFrameResult>;
            start: (config: FrameGenConfig) => Promise<DeepFrameResult>;
            stop: () => Promise<DeepFrameResult>;
            getStats: () => Promise<FrameStats | null>;
            isRunning: () => Promise<boolean>;
        };
        windowControls?: {
            minimize: () => void;
            maximize: () => void;
            close: () => void;
        };
    }
}

export function useDeepFrame() {
    const [isInitialized, setIsInitialized] = useState(false);
    const [isRunning, setIsRunning] = useState(false);
    const [stats, setStats] = useState<FrameStats | null>(null);
    const [error, setError] = useState<string | null>(null);
    const [isElectron, setIsElectron] = useState(false);

    
    useEffect(() => {
        setIsElectron(!!window.deepframe);
    }, []);

    
    useEffect(() => {
        if (!window.deepframe) return;

        const init = async () => {
            try {
                const result = await window.deepframe!.initialize();
                setIsInitialized(result.success);
                if (!result.success) {
                    setError(result.error || 'Initialization failed');
                }
            } catch (err) {
                setError(String(err));
                setIsInitialized(false);
            }
        };

        init();
    }, []);

    
    useEffect(() => {
        if (!isRunning || !window.deepframe) return;

        const interval = setInterval(async () => {
            try {
                const newStats = await window.deepframe!.getStats();
                setStats(newStats);
            } catch {
                
            }
        }, 100);

        return () => clearInterval(interval);
    }, [isRunning]);

    
    useEffect(() => {
        if (!isInitialized || !window.deepframe) return;

        const checkRunning = async () => {
            try {
                const running = await window.deepframe!.isRunning();
                setIsRunning(running);
            } catch {
                
            }
        };

        checkRunning();
        const interval = setInterval(checkRunning, 1000);
        return () => clearInterval(interval);
    }, [isInitialized]);

    const start = useCallback(async (config: FrameGenConfig = {}) => {
        if (!window.deepframe) {
            setError('Not running in Electron');
            return false;
        }

        try {
            const result = await window.deepframe.start(config);
            if (result.success) {
                setIsRunning(true);
                setError(null);
                return true;
            } else {
                setError(result.error || 'Start failed');
                return false;
            }
        } catch (err) {
            setError(String(err));
            return false;
        }
    }, []);

    const stop = useCallback(async () => {
        if (!window.deepframe) return false;

        try {
            await window.deepframe.stop();
            setIsRunning(false);
            setStats(null);
            return true;
        } catch (err) {
            setError(String(err));
            return false;
        }
    }, []);

    return {
        isElectron,
        isInitialized,
        isRunning,
        stats,
        error,
        start,
        stop,
    };
}