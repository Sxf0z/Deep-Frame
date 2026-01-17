

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

export class DeepFrame {
    constructor();
    initialize(): boolean;
    start(config?: FrameGenConfig): boolean;
    stop(): boolean;
    getStats(): FrameStats | null;
    isRunning(): boolean;
}