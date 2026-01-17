import React from 'react';
import '../styles/components.css';

interface ConfigPanelProps {
    title: string;
    children: React.ReactNode;
}

export const ConfigPanel: React.FC<ConfigPanelProps> = ({ title, children }) => {
    return (
        <div className="config-panel">
            <h3 className="panel-title">{title}</h3>
            <div className="panel-content">
                {children}
            </div>
        </div>
    );
};

export const ConfigRow: React.FC<{ label: string; children: React.ReactNode }> = ({ label, children }) => (
    <div className="config-row">
        <label className="config-label">{label}</label>
        <div className="config-input">
            {children}
        </div>
    </div>
);