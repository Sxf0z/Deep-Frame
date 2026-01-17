import React from 'react';
import '../styles/components.css';

interface ToggleSwitchProps {
    label?: string;
    checked?: boolean;
    onChange?: (checked: boolean) => void;
}

export const ToggleSwitch: React.FC<ToggleSwitchProps> = ({ label, checked = false, onChange }) => {
    return (
        <label className="toggle-switch-container">
            {label && <span className="toggle-label">{label}</span>}
            <div className={`toggle-track ${checked ? 'checked' : ''}`} onClick={() => onChange && onChange(!checked)}>
                <div className="toggle-thumb" />
            </div>
        </label>
    );
};