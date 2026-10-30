import React, { useState, useEffect } from 'react';
import { CreateChannelPage } from '../features/channels/pages/CreateChannelPage';
import { LicensePage } from '../features/license/pages/LicensePage';
import { Activity, Radio } from 'lucide-react';

type Tab = 'channel' | 'licence' | 'stats' | 'users' | 'network' | 'settings';

const TAB_LABELS: { id: Tab; label: string }[] = [
  { id: 'channel',  label: 'Channel Control' },
  { id: 'licence',  label: 'Licence' },
  { id: 'stats',    label: 'Stats' },
  { id: 'users',    label: 'Users' },
  { id: 'network',  label: 'Network' },
  { id: 'settings', label: 'Settings' },
];

export const App: React.FC = () => {
  const [currentDateTime, setCurrentDateTime] = useState<string>('');
  const [activeTab, setActiveTab] = useState<Tab>('channel');

  useEffect(() => {
    const updateTime = () => {
      const now = new Date();
      setCurrentDateTime(
        now.toLocaleString('en-US', {
          month: 'short',
          day: 'numeric',
          year: 'numeric',
          hour: '2-digit',
          minute: '2-digit',
          second: '2-digit',
          hour12: false,
        })
      );
    };
    updateTime();
    const interval = setInterval(updateTime, 1000);
    return () => clearInterval(interval);
  }, []);

  return (
    <div className="app-layout">
      {/* Top Application Header matching Figma frame 447:168 */}
      <header className="app-header">
        <div className="header-top">
          <div className="logo-area">
            <div className="logo-badge">
              <Radio size={18} />
            </div>
            <span>Skandha Transcoder</span>
          </div>

          <div className="header-user">
            <span style={{ fontFamily: 'JetBrains Mono', fontSize: '0.8125rem' }}>
              {currentDateTime || 'Loading clock...'}
            </span>
            <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', fontWeight: 600 }}>
              <Activity size={14} color="var(--color-success)" />
              <span>Operator (Active)</span>
            </div>
          </div>
        </div>

        {/* Navigation Tabs */}
        <nav className="header-tabs">
          {TAB_LABELS.map(({ id, label }) => (
            <button
              key={id}
              id={`tab-${id}`}
              className={`tab-btn${activeTab === id ? ' active' : ''}`}
              onClick={() => setActiveTab(id)}
            >
              {label}
            </button>
          ))}
        </nav>
      </header>

      {/* Main Content Body */}
      <main className="main-container">
        {activeTab === 'channel'  && <CreateChannelPage />}
        {activeTab === 'licence'  && <LicensePage />}
        {activeTab !== 'channel' && activeTab !== 'licence' && (
          <div style={{ textAlign: 'center', padding: '4rem 1rem', color: 'var(--color-text-muted)' }}>
            <p style={{ fontSize: '1rem', fontWeight: 600 }}>
              {TAB_LABELS.find(t => t.id === activeTab)?.label} — coming soon
            </p>
          </div>
        )}
      </main>
    </div>
  );
};
