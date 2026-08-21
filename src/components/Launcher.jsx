import React, { useState } from 'react';
import { ICONS, APPS } from './Dock';

const Launcher = ({ isOpen, onClose, onAppClick }) => {
  const [search, setSearch] = useState('');

  const filteredApps = APPS.filter(app => 
    app.name.toLowerCase().includes(search.toLowerCase())
  );

  if (!isOpen) return null;

  return (
    <div id="launcher" className="fixed inset-0 z-[150] bg-[rgba(4,7,14,0.6)] backdrop-blur-[30px] saturate-[140%] flex flex-col items-center pt-[9vh] animate-fade-in">
      <div className="disp text-[18px] tracking-[5px] text-white/[0.85]">ПРИЛОЖЕНИЯ</div>
      
      <div className="relative mt-[16px]">
        <input
          id="launchSearch"
          className="field min-w-[280px] pl-[38px]"
          style={{ width: 'min(340px, 84vw)' }}
          placeholder="Поиск приложений…"
          autoComplete="off"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
        />
        <svg viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="rgba(255,255,255,0.4)" strokeWidth="1.8" strokeLinecap="round" className="absolute left-3 top-1/2 -translate-y-1/2">
          <circle cx="11" cy="11" r="6.5" />
          <path d="M20 20l-4.2-4.2" />
        </svg>
      </div>
      
      <div className="launch-grid grid grid-cols-4 gap-[14px] mt-[26px]">
        {filteredApps.map(app => (
          <div
            key={app.id}
            className="launch-item rounded-xl px-[10px] py-[14px] flex flex-col items-center gap-[10px] cursor-pointer transition-all border border-white/[0.07] bg-white/[0.04] hover:-translate-y-1 hover:scale-[1.03] hover:bg-white/[0.09] hover:border-[color-mix(in_srgb,var(--accent)_45%,transparent)]"
            onClick={() => { onAppClick(app.id); onClose(); }}
          >
            <div className="li-ic w-[46px] h-[46px] rounded-xl flex items-center justify-center bg-gradient-to-br from-[color-mix(in_srgb,var(--accent)_30%,transparent)] to-[color-mix(in_srgb,var(--accent2)_25%,transparent)] text-white shadow-[0_8px_20px_color-mix(in_srgb,var(--accent)_25%,transparent)]">
              <div className="w-[22px] h-[22px]">{ICONS[app.icon]}</div>
            </div>
            <div className="text-[12.5px] font-semibold text-white">{app.name}</div>
            <div className="mono text-[9px] text-white/[0.35]">{app.tag}</div>
          </div>
        ))}
      </div>
      
      <div className="mono mt-[34px] text-[10px] text-white/[0.35]">Esc — закрыть · Enter — открыть первое</div>
    </div>
  );
};

export default Launcher;
