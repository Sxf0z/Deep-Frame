import React, { useState } from 'react';
import '../styles/components.css';




let ipcRenderer = { send: (_channel: string, ..._args: any[]) => { } };
try {
    
    if (window.require) {
        
        const electron = window.require('electron');
        
        if (typeof electron === 'object' && electron.ipcRenderer) {
            ipcRenderer = electron.ipcRenderer;
        } else {
            console.warn('Electron require returned unexpected type:', typeof electron);
        }
    }
} catch (e) {
    console.error('Failed to require electron:', e);
}

export const TitleBar: React.FC = () => {
    const [isMaximized, setIsMaximized] = useState(false);

    const handleMinimize = () => ipcRenderer.send('window-minimize');
    const handleMaximize = () => {
        ipcRenderer.send('window-maximize');
        setIsMaximized(!isMaximized);
    };
    const handleClose = () => ipcRenderer.send('window-close');

    return (
        <div className="titlebar">
            <div className="titlebar-drag">
                <span className="window-title">Deep Frame</span>
            </div>
            <div className="window-controls">
                <button className="control-btn minimize" onClick={handleMinimize} title="Minimize">
                    <svg width="10" height="1" viewBox="0 0 10 1"><rect width="10" height="1" fill="currentColor" /></svg>
                </button>
                <button className="control-btn maximize" onClick={handleMaximize} title="Maximize">
                    {isMaximized ? (
                        <svg width="10" height="10" viewBox="0 0 10 10"><path d="M2.1 0v2H0v8.1h8.2v-2h2V0H2.1zm5.1 8.1H1v-6h6v6zM9.2 7V2H3.1V1h6.1v6z" fill="currentColor" /></svg>
                    ) : (
                        <svg width="10" height="10" viewBox="0 0 10 10"><path d="M0 0v10h10V0H0zm9 9H1V1h8v8z" fill="currentColor" /></svg>
                    )}
                </button>
                <button className="control-btn close" onClick={handleClose} title="Close">
                    <svg width="10" height="10" viewBox="0 0 10 10"><path d="M1 0L0 1L4 5L0 9L1 10L5 6L9 10L10 9L6 5L10 1L9 0L5 4L1 0Z" fill="currentColor" /></svg>
                </button>
            </div>
        </div>
    );
};