import React from 'react';

const ICONS = {
  agents: (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round">
      <circle cx="9" cy="8" r="3.2" />
      <path d="M3.5 19c.6-3.2 2.8-5 5.5-5s4.9 1.8 5.5 5" />
      <circle cx="17" cy="9" r="2.6" />
      <path d="M15.5 14.4c2.3.2 4.2 1.8 4.8 4.6" />
    </svg>
  ),
  chat: (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
      <path d="M4 6a3 3 0 0 1 3-3h10a3 3 0 0 1 3 3v7a3 3 0 0 1-3 3H9l-5 4z" />
      <path d="M8.5 8.5h7M8.5 11.5h4.5" />
    </svg>
  ),
  terminal: (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
      <rect x="3" y="4" width="18" height="16" rx="3" />
      <path d="M7 9l3.5 3L7 15M13 15h4" />
    </svg>
  ),
  monitor: (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
      <path d="M3 12h4l3-7 4 14 3-7h4" />
    </svg>
  ),
  files: (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinejoin="round">
      <path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z" />
    </svg>
  ),
  settings: (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round">
      <circle cx="12" cy="12" r="3.2" />
      <path d="M12 2.8v3M12 18.2v3M21.2 12h-3M5.8 12h-3M18.5 5.5l-2.1 2.1M7.6 16.4l-2.1 2.1M18.5 18.5l-2.1-2.1M7.6 7.6L5.5 5.5" />
    </svg>
  ),
  about: (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round">
      <circle cx="12" cy="12" r="9" />
      <path d="M12 10.8V17M12 7v.2" />
    </svg>
  )
};

const APPS = [
  { id: 'agents', name: 'Агенты', icon: 'agents', tag: 'CAP · IPC' },
  { id: 'chat', name: 'Чат', icon: 'chat', tag: 'agent-native' },
  { id: 'terminal', name: 'Терминал', icon: 'terminal', tag: 'shell · UTF-8' },
  { id: 'monitor', name: 'Монитор', icon: 'monitor', tag: 'PIT 100 Гц' },
  { id: 'files', name: 'Файлы', icon: 'files', tag: 'VFS + initrd' },
  { id: 'settings', name: 'Настройки', icon: 'settings', tag: 'system' },
  { id: 'about', name: 'О системе', icon: 'about', tag: 'v0.5' }
];

const Dock = ({ activeApp, onAppClick }) => (
  <div id="dock" className="fixed left-[10px] top-1/2 -translate-y-1/2 z-[90] flex flex-col gap-[6px] p-[10px] rounded-[18px] glass">
    {APPS.map(app => (
      <button
        key={app.id}
        className={`w-11 h-11 rounded-xl flex items-center justify-center text-[#c3cdf0] relative transition-all cursor-pointer border border-transparent bg-transparent
          ${activeApp === app.id ? 'bg-white/[0.1] border-white/[0.14] text-white' : ''}
          hover:bg-white/[0.09] hover:scale-108 hover:text-white`}
        onClick={() => onAppClick(app.id)}
        title={app.name}
      >
        <div className="w-5 h-5">{ICONS[app.icon]}</div>
        <span className="absolute bottom-1 left-1/2 -translate-x-1/2 w-1 h-1 rounded-full bg-accent2 opacity-0 transition-all box-shadow-[0_0_6px_var(--accent2)]" />
      </button>
    ))}
  </div>
);

export { APPS, ICONS };
export default Dock;
