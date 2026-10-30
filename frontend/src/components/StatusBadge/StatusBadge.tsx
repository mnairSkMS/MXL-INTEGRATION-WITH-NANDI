import React from 'react';
import { SupervisorState } from '../../features/channels/types/view';

export interface StatusBadgeProps {
  state: SupervisorState | string;
}

export const StatusBadge: React.FC<StatusBadgeProps> = ({ state }) => {
  let badgeClass = 'badge-neutral';
  let label = state;

  switch (state) {
    case 'RUNNING':
      badgeClass = 'badge-success';
      label = '● RUNNING';
      break;
    case 'STARTING':
      badgeClass = 'badge-info';
      label = '◌ STARTING';
      break;
    case 'BACKOFF':
    case 'FATAL':
      badgeClass = 'badge-danger';
      label = `▲ ${state}`;
      break;
    case 'STOPPED':
      badgeClass = 'badge-neutral';
      label = '■ STOPPED';
      break;
    default:
      badgeClass = 'badge-neutral';
      label = state;
  }

  return <span className={`badge ${badgeClass}`}>{label}</span>;
};
