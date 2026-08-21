import React, { useState, useEffect } from 'react';

const TopBar = ({ ramPct, cpuPct, uptime }) => {
  const [time, setTime] = useState(new Date());

  useEffect(() => {
    const timer = setInterval(() => setTime(new Date()), 1000);
    return () => clearInterval(timer);
  }, []);

  const formatTime = (date) => date.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  const formatDate = (date) => date.toLocaleDateString('ru-RU', { weekday: 'short', day: 'numeric', month: 'short' });

  return (
    <div id="topbar" className="fixed top-0 left-0 right-0 h-[38px] z-[90] flex items-center justify-between px-[10px] bg-[rgba(7,10,19,0.55)] backdrop-blur-[20px] saturate-[140%] border-b border-white/[0.06]">
      <div className="flex items-center gap-2">
        <button className="w-7 h-7 rounded-lg flex items-center justify-center text-[#cfd8f2] hover:bg-white/[0.1] hover:text-white transition-all cursor-pointer border-none bg-transparent" title="Лаунчер приложений">
          <svg viewBox="0 0 48 48" width="20" height="20">
            <defs>
              <linearGradient id="lgTop" x1="0" y1="0" x2="1" y2="1">
                <stop offset="0" stopColor="#8b7bff" />
                <stop offset="1" stopColor="#22d3ee" />
              </linearGradient>
            </defs>
            <path d="M24 3 L41 12.5v19L24 41 7 31.5v-19z" fill="none" stroke="url(#lgTop)" strokeWidth="3" />
            <path d="M18 15v18M18 24l12-9M18 24l12 9" stroke="url(#lgTop)" strokeWidth="3" strokeLinecap="round" fill="none" />
          </svg>
        </button>
        <span className="disp text-[12px] tracking-[2px] bg-gradient-to-r from-white to-[#9aa7ff] bg-clip-text text-transparent">KENGAOS</span>
        <span className="chip mono hidden md:flex text-[9px]">0.5 · agent-native</span>
      </div>
      
      <div className="absolute left-1/2 -translate-x-1/2 flex items-baseline gap-[10px]">
        <span id="clockTime" className="mono text-[13px] font-semibold text-white">{formatTime(time)}</span>
        <span id="clockDate" className="text-[11px] text-white/50 hidden md:inline">{formatDate(time)}</span>
      </div>
      
      <div className="flex items-center gap-[6px]">
        <span className="chip mono hidden md:flex text-[11px] py-[3px] px-[9px] rounded-lg bg-white/[0.06] text-[#cfd8f2]" title="CPU">
          CPU <b className="text-white font-semibold">{cpuPct}%</b>
        </span>
        <span className="chip mono flex text-[11px] py-[3px] px-[9px] rounded-lg bg-white/[0.06] text-[#cfd8f2]" title="Память ядра">
          <span className="hidden md:inline">RAM</span>
          <span className="w-[52px] h-1 rounded bg-white/[0.12] overflow-hidden ml-1">
            <i className="block h-full w-[40%] bg-gradient-to-r from-accent to-accent2 transition-all duration-600" style={{ width: `${ramPct}%` }} />
          </span>
          <b className="text-white font-semibold ml-1">{ramPct}%</b>
        </span>
        <span className="chip mono hidden md:flex text-[11px] py-[3px] px-[9px] rounded-lg bg-white/[0.06] text-[#cfd8f2]" title="Время работы">
          ⏱ {uptime}
        </span>
        <button className="w-7 h-7 rounded-lg flex items-center justify-center text-[#cfd8f2] hover:bg-white/[0.1] hover:text-white transition-all cursor-pointer border-none bg-transparent" title="Питание">
          <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round">
            <path d="M12 3v8M6.3 6.5a8 8 0 1 0 11.4 0" />
          </svg>
        </button>
      </div>
    </div>
  );
};

export default TopBar;
