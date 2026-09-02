import React, { useState, useEffect, useRef } from 'react';
import { ICONS, APPS } from '../components/Dock.jsx';
import TerminalWindow from '../components/TerminalWindow.jsx';

/* KengaOS Mobile — тач-оболочка референса (glass / воздушная).
   Одно «ядро» (kernel bridge внизу файла) — та же поверхность, что у десктопа:
   uptime, cpu/ram, агенты, IPC-лог. Точный порт в Kenga — по design.md. */

/* ---------- kernel bridge (локальная симуляция той же поверхности) ---------- */

const useKernel = (booted) => {
  const [k, setK] = useState({
    uptime: 0, cpu: 12, ram: 41, agents: 4, ipc: [],
    now: new Date(),
  });
  useEffect(() => {
    if (!booted) return;
    const t0 = Date.now();
    const lines = [
      ['init', 'ui-agent', 'mobile shell готов'],
      ['ui-agent', 'model-agent', 'query XOR'],
      ['model-agent', 'you', 'ответ доставлен'],
      ['kenga-agent', 'vfs', 'initrd смонтирован'],
    ];
    let li = 0;
    const t = setInterval(() => {
      const up = Math.floor((Date.now() - t0) / 1000);
      setK(k => ({
        ...k,
        uptime: up,
        now: new Date(),
        cpu: Math.min(95, Math.max(5, k.cpu + (Math.random() - 0.5) * 6)),
        ram: Math.min(95, Math.max(25, k.ram + (Math.random() - 0.5) * 3)),
        ipc: [...k.ipc.slice(-3), lines[li++ % lines.length]],
      }));
    }, 1000);
    return () => clearInterval(t);
  }, [booted]);
  return k;
};

const fmt = (s) => [3600, 60].reduce((a, m) => { const r = a % m; a = (a - r) / m; return [...a, r]; }, [s % 60]).map(n => String(n).padStart(2, '0')).join(':');

/* ---------- обои: база + два дрейфующих glow + vignette ---------- */

const Wallpaper = () => (
  <div className="absolute inset-0 overflow-hidden bg-bg">
    <div id="wallGlow" className="absolute inset-0">
      <i className="animate-drift1 absolute block rounded-full bg-accent" style={{ width: '55vw', height: '55vw', filter: 'blur(90px)', opacity: 0.16, top: '-10%', left: '-15%', mixBlendMode: 'screen' }} />
      <i className="animate-drift2 absolute block rounded-full bg-accent2" style={{ width: '48vw', height: '48vw', filter: 'blur(90px)', opacity: 0.13, bottom: '-12%', right: '-12%', mixBlendMode: 'screen' }} />
    </div>
    <div id="vignette" className="absolute inset-0" style={{ background: 'radial-gradient(ellipse at center, transparent 55%, rgba(0,0,0,0.55) 100%)' }} />
  </div>
);

/* ---------- статусбар ---------- */

const StatusBar = ({ k }) => (
  <div className="flex items-center justify-between px-5 pt-3 pb-1 text-white/70">
    <span className="mono text-[11px] tracking-wide">{k.now.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' })}</span>
    <div className="flex items-center gap-2">
      <span className="mono text-[9px] text-white/40">KENGAOS</span>
      <span className="statdot animate-blink inline-block h-1.5 w-1.5 rounded-full" />
      <span className="mono text-[9px] text-white/40">CPU {Math.round(k.cpu)}%</span>
    </div>
  </div>
);

/* ---------- lock screen ---------- */

const LockScreen = ({ k, onUnlock }) => {
  const startY = useRef(null);
  const dy = useRef(0);
  const [drag, setDrag] = useState(0);
  const onDown = (e) => { startY.current = (e.touches ? e.touches[0] : e).clientY; };
  const onMove = (e) => {
    if (startY.current === null) return;
    const y = (e.touches ? e.touches[0] : e).clientY;
    dy.current = Math.max(0, Math.min(140, startY.current - y));
    setDrag(dy.current);
  };
  const onUp = () => { if (dy.current > 60) onUnlock(); startY.current = null; dy.current = 0; setDrag(0); };
  return (
    <div
      className="absolute inset-0 flex flex-col select-none"
      style={{ transform: `translateY(${-drag}px)`, transition: startY.current === null ? 'transform 0.3s ease' : 'none' }}
      onTouchStart={onDown} onTouchMove={onMove} onTouchEnd={onUp}
      onMouseDown={onDown} onMouseMove={onMove} onMouseUp={onUp} onMouseLeave={onUp}
    >
      <div className="mt-24 flex flex-col items-center">
        <div className="disp text-7xl font-extralight tracking-tight text-white/95">
          {k.now.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' })}
        </div>
        <div className="mt-2 text-sm text-white/55">
          {k.now.toLocaleDateString('ru-RU', { weekday: 'long', day: 'numeric', month: 'long' })}
        </div>
      </div>
      <div className="glass mx-6 mt-10 rounded-2xl px-5 py-4">
        <div className="flex items-center gap-2">
          <span className="statdot animate-blink inline-block h-1.5 w-1.5 rounded-full" />
          <span className="mono text-[10px] tracking-wider text-white/60">ЖИВОЙ ЛОГ · IPC</span>
        </div>
        <div className="mono mt-2 space-y-1 text-[10px] leading-relaxed text-white/50">
          {k.ipc.slice(-2).map(([a, b, m], i) => (
            <div key={i}><span className="text-accent2">[{a}]</span> → <span className="text-accent">{b}</span> · {m}</div>
          ))}
        </div>
      </div>
      <div className="mt-auto pb-10 flex flex-col items-center">
        <div className="text-[11px] text-white/40 animate-bounce">↑ разблокировать</div>
      </div>
    </div>
  );
};

/* ---------- карточка-виджет ---------- */

const Widget = ({ k }) => (
  <div className="glass rounded-2xl px-5 py-4">
    <div className="flex items-baseline justify-between">
      <div className="disp text-3xl font-light text-white/95">
        {k.now.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' })}
      </div>
      <div className="mono text-[10px] text-white/45">
        {k.now.toLocaleDateString('ru-RU', { day: '2-digit', month: '2-digit' })}
      </div>
    </div>
    <div className="mt-3 flex gap-2">
      <div className="badge flex-1 rounded-lg px-2 py-1 text-center">CPU {Math.round(k.cpu)}%</div>
      <div className="badge flex-1 rounded-lg px-2 py-1 text-center">RAM {Math.round(k.ram)}%</div>
      <div className="badge flex-1 rounded-lg px-2 py-1 text-center">UP {fmt(k.uptime)}</div>
    </div>
  </div>
);

/* ---------- приложение на весь экран ---------- */

const AppSheet = ({ app, onClose }) => (
  <div className="animate-pop-in absolute inset-0 z-40 flex flex-col">
    <div className="glass m-2 flex flex-1 flex-col overflow-hidden rounded-3xl">
      <div className="flex items-center gap-2 border-b border-white/[0.06] px-4 py-3">
        <button onClick={onClose} className="h-3 w-3 rounded-full bg-[#ff5f57] active:scale-90" aria-label="закрыть" />
        <span className="h-3 w-3 rounded-full bg-white/15" />
        <span className="ml-2 flex items-center gap-2 text-sm text-white/85">
          <span className="h-4 w-4 text-accent">{ICONS[app.icon]}</span>
          {app.name}
        </span>
        <span className="badge ml-auto hidden px-1.5 py-0.5 text-[9px] sm:block">{app.tag}</span>
      </div>
      <div className="flex-1 overflow-auto">
        {app.id === 'terminal'
          ? <TerminalWindow />
          : <div className="flex h-full flex-col items-center justify-center gap-3 text-white/35">
              <span className="h-10 w-10 text-accent/60">{ICONS[app.icon]}</span>
              <span className="mono text-[11px]">модуль «{app.name}» · этап порта в Kenga</span>
            </div>}
      </div>
    </div>
  </div>
);

/* ---------- домашний экран ---------- */

const Home = ({ k, onOpen, onLauncher }) => {
  const dock = ['agents', 'chat', 'terminal', 'files'];
  return (
    <div className="animate-fade-in absolute inset-0 flex flex-col">
      <StatusBar k={k} />
      <div className="px-4 pt-2"><Widget k={k} /></div>
      <div className="grid grid-cols-4 gap-x-2 gap-y-5 px-4 pt-6">
        {APPS.map(app => (
          <button key={app.id} onClick={() => onOpen(app)} className="group flex flex-col items-center gap-1.5">
            <span className="glass flex h-14 w-14 items-center justify-center rounded-2xl text-accent transition group-active:scale-90 group-active:text-accent2">
              <span className="h-6 w-6">{ICONS[app.icon]}</span>
            </span>
            <span className="text-[10px] text-white/60">{app.name}</span>
          </button>
        ))}
      </div>
      <div className="mt-auto px-4 pb-2">
        <div className="glass flex items-center justify-around rounded-3xl px-2 py-3">
          {dock.map(id => {
            const app = APPS.find(a => a.id === id);
            return (
              <button key={id} onClick={() => onOpen(app)} className="text-accent transition active:scale-90">
                <span className="h-7 w-7">{ICONS[app.icon]}</span>
              </button>
            );
          })}
        </div>
        <button onClick={onLauncher} className="mx-auto mt-3 block h-1.5 w-28 rounded-full bg-white/30 active:bg-white/60" aria-label="лаунчер" />
      </div>
    </div>
  );
};

/* ---------- лаунчер ---------- */

const Launcher = ({ onOpen, onClose }) => {
  const [q, setQ] = useState('');
  const apps = APPS.filter(a => a.name.toLowerCase().includes(q.toLowerCase()) || a.id.includes(q.toLowerCase()));
  return (
    <div className="animate-fade-in absolute inset-0 z-30 flex flex-col bg-[rgba(4,7,14,0.65)] backdrop-blur-2xl saturate-150">
      <div className="px-4 pt-6">
        <input
          autoFocus
          value={q}
          onChange={e => setQ(e.target.value)}
          placeholder="поиск модуля…"
          className="field w-full rounded-xl px-4 py-3 text-sm outline-none"
        />
      </div>
      <div className="grid grid-cols-4 gap-x-2 gap-y-5 px-4 pt-8">
        {apps.map(app => (
          <button key={app.id} onClick={() => onOpen(app)} className="group flex flex-col items-center gap-1.5">
            <span className="flex h-14 w-14 items-center justify-center rounded-2xl border border-white/[0.08] bg-white/[0.04] text-accent transition group-active:scale-90">
              <span className="h-6 w-6">{ICONS[app.icon]}</span>
            </span>
            <span className="text-[10px] text-white/60">{app.name}</span>
          </button>
        ))}
        {!apps.length && <div className="col-span-4 py-8 text-center text-xs text-white/30">ничего не найдено</div>}
      </div>
      <button onClick={onClose} className="mx-auto mt-auto mb-4 block h-1.5 w-28 rounded-full bg-white/40" aria-label="домой" />
    </div>
  );
};

/* ---------- корень ---------- */

const Mobile = () => {
  const [booted, setBooted] = useState(false);
  const [locked, setLocked] = useState(true);
  const [launcher, setLauncher] = useState(false);
  const [openApp, setOpenApp] = useState(null);
  const k = useKernel(booted);

  const open = (app) => { setLauncher(false); setOpenApp(app); };
  const home = () => { setOpenApp(null); setLauncher(false); };

  return (
    <div className="relative mx-auto h-screen max-w-[430px] overflow-hidden">
      <Wallpaper />
      {booted && (
        <>
          {locked
            ? <LockScreen k={k} onUnlock={() => setLocked(false)} />
            : <Home k={k} onOpen={open} onLauncher={() => setLauncher(true)} />}
          {launcher && <Launcher onOpen={open} onClose={home} />}
          {openApp && <AppSheet app={openApp} onClose={home} />}
        </>
      )}
      {!booted && (
        <div className="absolute inset-0 z-50 flex items-center justify-center" onClick={() => setBooted(true)}>
          <BootMobile onDone={() => setBooted(true)} />
        </div>
      )}
    </div>
  );
};

/* компактный boot: логотип + прогресс, тап — пропустить */
const BootMobile = ({ onDone }) => {
  const [p, setP] = useState(0);
  useEffect(() => {
    const t = setInterval(() => setP(v => {
      if (v >= 100) { clearInterval(t); setTimeout(onDone, 350); return 100; }
      return v + 4;
    }), 40);
    return () => clearInterval(t);
  }, []);
  return (
    <div className="flex w-64 flex-col items-center gap-5" onClick={onDone}>
      <div className="disp text-2xl font-light tracking-[0.3em] text-white/90">KENGAOS</div>
      <div className="h-[3px] w-full overflow-hidden rounded-full bg-white/10">
        <div className="h-full rounded-full transition-all duration-100" style={{ width: `${p}%`, background: 'linear-gradient(90deg, var(--accent), var(--accent2))' }} />
      </div>
      <div className="mono text-[9px] tracking-widest text-white/30">mobile · aarch64</div>
    </div>
  );
};

export default Mobile;
