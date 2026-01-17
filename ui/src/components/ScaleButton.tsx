import React, { useState, useEffect } from 'react';
import '../styles/components.css';

interface ScaleButtonProps {
    onScaleStart: () => void;
    onScaleStop: () => void;
    disabled?: boolean;
    isRunning?: boolean;
}

export const ScaleButton: React.FC<ScaleButtonProps> = ({
    onScaleStart,
    onScaleStop,
    disabled = false,
    isRunning: externalIsRunning,
}) => {
    const [countdown, setCountdown] = useState<number | null>(null);

    
    const isRunning = externalIsRunning ?? false;

    const handleClick = () => {
        if (disabled) return;

        if (isRunning) {
            onScaleStop();
            return;
        }
        setCountdown(5);
    };

    useEffect(() => {
        if (countdown === null) return;
        if (countdown > 0) {
            const timer = setTimeout(() => setCountdown(countdown - 1), 1000);
            return () => clearTimeout(timer);
        } else {
            setCountdown(null);
            onScaleStart();
        }
    }, [countdown, onScaleStart]);

    
    useEffect(() => {
        if (disabled && countdown !== null) {
            setCountdown(null);
        }
    }, [disabled, countdown]);

    return (
        <button
            className={`scale-button ${isRunning ? 'running' : ''} ${countdown !== null ? 'counting' : ''}`}
            onClick={handleClick}
            disabled={disabled}
            style={{ opacity: disabled ? 0.5 : 1 }}
        >
            {countdown !== null ? (
                <span>Scale in {countdown}s</span>
            ) : isRunning ? (
                <span>Stop</span>
            ) : (
                <span>Scale</span>
            )}
        </button>
    );
};