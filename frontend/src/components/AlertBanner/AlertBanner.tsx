import React from 'react';
import { AlertCircle, CheckCircle, Info, AlertTriangle } from 'lucide-react';

export interface AlertBannerProps {
  type: 'danger' | 'warning' | 'info' | 'success';
  title?: string;
  message: React.ReactNode;
  action?: React.ReactNode;
}

export const AlertBanner: React.FC<AlertBannerProps> = ({ type, title, message, action }) => {
  const icons = {
    danger: <AlertCircle size={20} className="text-danger flex-shrink-0" />,
    warning: <AlertTriangle size={20} className="text-warning flex-shrink-0" />,
    info: <Info size={20} className="text-info flex-shrink-0" />,
    success: <CheckCircle size={20} className="text-success flex-shrink-0" />,
  };

  return (
    <div className={`alert-banner alert-${type}`}>
      {icons[type]}
      <div style={{ flex: 1 }}>
        {title && <div style={{ fontWeight: 700, marginBottom: '0.25rem' }}>{title}</div>}
        <div>{message}</div>
      </div>
      {action && <div>{action}</div>}
    </div>
  );
};
