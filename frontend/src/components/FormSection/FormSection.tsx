import React, { useState } from 'react';
import { ChevronDown, ChevronRight } from 'lucide-react';

export interface FormSectionProps {
  title: string;
  badge?: React.ReactNode;
  action?: React.ReactNode;
  defaultExpanded?: boolean;
  children: React.ReactNode;
}

export const FormSection: React.FC<FormSectionProps> = ({
  title,
  badge,
  action,
  defaultExpanded = true,
  children,
}) => {
  const [expanded, setExpanded] = useState(defaultExpanded);

  return (
    <div className="card">
      <div
        className="card-header"
        style={{ cursor: 'pointer', userSelect: 'none' }}
        onClick={() => setExpanded(!expanded)}
      >
        <div className="card-title">
          {expanded ? <ChevronDown size={18} /> : <ChevronRight size={18} />}
          <span>{title}</span>
          {badge}
        </div>
        {action && (
          <div onClick={(e) => e.stopPropagation()}>
            {action}
          </div>
        )}
      </div>
      {expanded && <div className="card-body">{children}</div>}
    </div>
  );
};
