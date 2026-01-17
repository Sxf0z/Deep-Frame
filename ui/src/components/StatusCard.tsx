import React from 'react';
import '../styles/components.css';

interface StatusCardProps {
    label: string;
    value: string | number;
    unit?: string;
}

export const StatusCard: React.FC<StatusCardProps> = ({ label, value, unit }) => {
    return (
        <div className="status-card animate-fade-in">
            <span className="status-label">{label}</span>
            <span className="status-value">
                {value}{unit && <span style={{ fontSize: '0.8em', marginLeft: '2px', opacity: 0.8 }}>{unit}</span>}
            </span>
        </div>
    );
};