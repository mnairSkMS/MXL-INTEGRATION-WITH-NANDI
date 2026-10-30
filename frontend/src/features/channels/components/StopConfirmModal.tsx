import React from 'react';
import { Modal } from '../../../components/Modal/Modal';
import { Button } from '../../../components/Button/Button';
import { AlertTriangle } from 'lucide-react';

export interface StopConfirmModalProps {
  isOpen: boolean;
  onClose: () => void;
  onConfirm: () => void;
  isLoading: boolean;
}

/**
 * Separate from RestartConfirmModal on purpose.
 *
 * A restart is disruptive but self-healing - it ends with the channel back on air, which is
 * why that modal can promise "about 10 seconds". A stop is not: nothing brings the channel
 * back but an explicit start, and telling an operator otherwise would be the one thing this
 * dialog exists to prevent.
 */
export const StopConfirmModal: React.FC<StopConfirmModalProps> = ({
  isOpen,
  onClose,
  onConfirm,
  isLoading,
}) => {
  return (
    <Modal
      isOpen={isOpen}
      onClose={isLoading ? () => {} : onClose}
      title="Stop the transcoder engine?"
      footer={
        <>
          <Button variant="secondary" onClick={onClose} disabled={isLoading}>
            Cancel
          </Button>
          <Button variant="danger" onClick={onConfirm} isLoading={isLoading}>
            Stop Engine &amp; Go Off Air
          </Button>
        </>
      }
    >
      <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem' }}>
        <div className="alert-banner alert-warning" style={{ margin: 0 }}>
          <AlertTriangle size={24} className="flex-shrink-0" />
          <div>
            <div style={{ fontWeight: 700, marginBottom: '0.25rem' }}>
              This takes the channel off air and leaves it there
            </div>
            <div>
              Unlike a restart, nothing brings the broadcast back automatically. It stays down
              until someone presses <strong>Start Engine</strong>.
            </div>
          </div>
        </div>

        <p style={{ fontSize: '0.875rem', color: 'var(--color-text-secondary)' }}>
          The management API, this page and the operator UI all stay available — they run as a
          separate process from the engine. Stopping releases the engine's input sockets, which
          is what makes the current source safe to probe.
        </p>
      </div>
    </Modal>
  );
};
