import React from 'react';
import { Modal } from '../../../components/Modal/Modal';
import { Button } from '../../../components/Button/Button';
import { AlertTriangle } from 'lucide-react';

export interface RestartConfirmModalProps {
  isOpen: boolean;
  onClose: () => void;
  onConfirm: () => void;
  isLoading: boolean;
}

export const RestartConfirmModal: React.FC<RestartConfirmModalProps> = ({
  isOpen,
  onClose,
  onConfirm,
  isLoading,
}) => {
  return (
    <Modal
      isOpen={isOpen}
      onClose={isLoading ? () => {} : onClose}
      title="Confirm Channel Restart"
      footer={
        <>
          <Button variant="secondary" onClick={onClose} disabled={isLoading}>
            Cancel
          </Button>
          <Button variant="danger" onClick={onConfirm} isLoading={isLoading}>
            Confirm & Take Off Air
          </Button>
        </>
      }
    >
      <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem' }}>
        <div className="alert-banner alert-warning" style={{ margin: 0 }}>
          <AlertTriangle size={24} className="flex-shrink-0" />
          <div>
            <div style={{ fontWeight: 700, marginBottom: '0.25rem' }}>
              Warning: Channel is Currently Live
            </div>
            <div>
              Restarting the transcoder engine will terminate active media pipelines and take the broadcast
              <strong> off air for approximately 10 seconds</strong> while new pipelines initialize.
            </div>
          </div>
        </div>

        <p style={{ fontSize: '0.875rem', color: 'var(--color-text-secondary)' }}>
          Do not close or reload this window during the restart sequence. Status will update automatically upon completion.
        </p>
      </div>
    </Modal>
  );
};
