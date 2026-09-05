import React, { useState, useEffect, useRef } from 'react';
import { ICONS, APPS } from '../components/Dock.jsx';
import { BROWSER_PAGES, HOME, parseAddr } from '../components/BrowserApp.jsx';
import { THEMES, getTheme, setTheme, initTheme } from '../theme.js';

/* URL-параметры: ?skip, ?shade, ?theme=aurora|blue|green, ?app=phone|browser|terminal|… */
const MOBILE_QS = typeof window !== 'undefined'
  ? new URLSearchParams(window.location.search) : new URLSearchParams();
if (MOBILE_QS.get('theme')) setTheme(MOBILE_QS.get('theme')); else initTheme();

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

const fmt = (s) => {
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  return [h, m, sec].map((n) => String(n).padStart(2, "0")).join(":");
};

/* ---------- иконки телефонного дока (трубка, смс, браузер, камера) ---------- */

const IconPhone = () => (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
    <path d="M22 16.9v3a2 2 0 0 1-2.2 2 19.8 19.8 0 0 1-8.6-3.1 19.5 19.5 0 0 1-6-6A19.8 19.8 0 0 1 2.1 4.2 2 2 0 0 1 4.1 2h3a2 2 0 0 1 2 1.7c.1 1 .4 2 .7 2.8a2 2 0 0 1-.5 2.1L8.1 9.9a16 16 0 0 0 6 6l1.3-1.2a2 2 0 0 1 2.1-.5c.9.3 1.9.6 2.8.7a2 2 0 0 1 1.7 2z" />
  </svg>
);
const IconMsg = () => (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
    <path d="M21 11.5a8.4 8.4 0 0 1-8.5 8.4 8.6 8.6 0 0 1-3.8-.9L3 21l2-5.3a8.4 8.4 0 1 1 16-4.2z" />
  </svg>
);
const IconBrowser = () => (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
    <circle cx="12" cy="12" r="9" />
    <path d="M3 12h18M12 3a14 14 0 0 1 0 18a14 14 0 0 1 0-18zM12 3a14 14 0 0 0 0 18a14 14 0 0 0 0-18z" />
  </svg>
);
const IconCamera = () => (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
    <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z" />
    <circle cx="12" cy="13" r="4" />
  </svg>
);
const IconBack = () => (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
    <path d="M19 12H5M12 19l-7-7 7-7" />
  </svg>
);

/* телефонные приложения дока (v1 — заглушки до порта в Kenga) */
const PHONE_DOCK = [
  { id: 'phone', name: 'Телефон', icon: <IconPhone /> },
  { id: 'messages', name: 'Сообщения', icon: <IconMsg /> },
  { id: 'browser', name: 'Браузер', icon: <IconBrowser /> },
  { id: 'camera', name: 'Камера', icon: <IconCamera /> },
];

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

/* ---------- статусбар (тап/драг — шторка) ---------- */

const StatusBar = ({ k, onShade }) => (
  <div
    className="flex cursor-pointer items-center justify-between px-5 pt-3 pb-1 text-white/70 transition hover:text-white/90"
    onClick={onShade}
    onTouchStart={(e) => { e.stopPropagation(); onShade(); }}
  >
    <span className="mono text-[11px] tracking-wide">{k.now.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' })}</span>
    <div className="flex items-center gap-2">
      <span className="mono text-[9px] text-white/40">KENGAOS</span>
      <span className="statdot animate-blink inline-block h-1.5 w-1.5 rounded-full" />
      <span className="mono text-[9px] text-white/40">CPU {Math.round(k.cpu)}%</span>
    </div>
  </div>
);

/* ---------- шторка быстрых настроек ---------- */

const Shade = ({ open, onClose }) => {
  const [toggles, setToggles] = useState({ wifi: true, bt: false, flash: false, air: false });
  const [bright, setBright] = useState(70);
  const [theme, setT] = useState(getTheme());
  const tgl = (k) => setToggles(t => ({ ...t, [k]: !t[k] }));
  const tile = (k, label, on) => (
    <button
      onClick={() => tgl(k)}
      className={`flex flex-col items-center gap-1.5 rounded-2xl px-3 py-3 transition active:scale-95 ${
        on ? 'bg-accent text-white' : 'bg-white/[0.06] text-white/50'}`}
    >
      <span className="h-5 w-5">{label}</span>
      <span className="text-[9px]">{k === 'wifi' ? 'Wi-Fi' : k === 'bt' ? 'BT' : k === 'flash' ? 'Фонарь' : 'Полёт'}</span>
    </button>
  );
  const icoWifi = <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round"><path d="M5 12a10 10 0 0 1 14 0M8.5 15.5a5 5 0 0 1 7 0M2 8.5a15 15 0 0 1 20 0" /><circle cx="12" cy="19" r="1" fill="currentColor" /></svg>;
  const icoBt = <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round"><path d="M6.5 6.5l11 11L12 23V1l5.5 5.5-11 11" /></svg>;
  const icoFlash = <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round"><path d="M13 2L3 14h7l-1 8 10-12h-7l1-8z" /></svg>;
  const icoAir = <svg viewBox="0 0 24 24" fill="currentColor"><path d="M21 16v-2l-8-5V3.5A1.5 1.5 0 0 0 11.5 2 1.5 1.5 0 0 0 10 3.5V9l-8 5v2l8-2.5V19l-2 1.5V22l3.5-1 3.5 1v-1.5L13 19v-5.5l8 2.5z" /></svg>;
  return (
    <div className="animate-fade-in absolute inset-x-0 top-0 z-40">
      <div className="glass m-2 rounded-3xl p-4 shadow-2xl">
        <div className="flex items-center justify-between px-1 pb-3">
          <span className="mono text-[10px] tracking-wider text-white/50">БЫСТРЫЕ НАСТРОЙКИ</span>
          <button onClick={onClose} className="mono text-[10px] text-white/40 hover:text-white/80">закрыть ✕</button>
        </div>
        <div className="grid grid-cols-4 gap-2">
          {tile('wifi', icoWifi, toggles.wifi)}
          {tile('bt', icoBt, toggles.bt)}
          {tile('flash', icoFlash, toggles.flash)}
          {tile('air', icoAir, toggles.air)}
        </div>
        <div className="mt-4 flex items-center gap-3 px-1">
          <span className="text-[10px] text-white/40">☀</span>
          <input
            type="range" min="10" max="100" value={bright}
            onChange={(e) => setBright(Number(e.target.value))}
            className="h-1.5 flex-1 appearance-none rounded-full bg-white/15 accent-[var(--accent)]"
            style={{ background: `linear-gradient(90deg, var(--accent) ${bright}%, rgba(255,255,255,0.15) ${bright}%)` }}
          />
          <span className="text-[10px] text-white/40">☀☀</span>
        </div>
        <div className="mt-4 flex items-center justify-between rounded-2xl bg-white/[0.04] px-4 py-2.5">
          <span className="mono text-[10px] text-white/45">Тема</span>
          <div className="flex items-center gap-2.5">
            {THEMES.map(t => (
              <button
                key={t.id}
                onClick={() => { setTheme(t.id); setT(t.id); }}
                title={t.name}
                className="h-5 w-5 rounded-full transition active:scale-90"
                style={{
                  background: t.id === 'aurora' ? 'linear-gradient(135deg,#8b7bff,#22d3ee)'
                    : t.id === 'blue' ? 'linear-gradient(135deg,#3fa4ff,#2ee6c8)'
                    : 'linear-gradient(135deg,#2fe3a0,#49c9ff)',
                  border: theme === t.id ? '2px solid rgba(255,255,255,0.9)' : '1px solid rgba(255,255,255,0.25)',
                }}
              />
            ))}
          </div>
        </div>
        <div className="mt-2 flex items-center justify-between rounded-2xl bg-white/[0.04] px-4 py-2.5">
          <span className="mono text-[10px] text-white/45">Яркость {bright}%</span>
          <span className="mono text-[10px] text-accent2">{toggles.wifi ? 'wifi: on' : 'wifi: off'}</span>
        </div>
      </div>
      <button onClick={onClose} className="mx-auto mt-2 block h-1.5 w-28 rounded-full bg-white/30" aria-label="закрыть шторку" />
    </div>
  );
};

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

/* ---------- экранная клавиатура (Ру/En, glass) ---------- */

const KB_RU = [
  ['й', 'ц', 'у', 'к', 'е', 'н', 'г', 'ш', 'щ', 'з', 'х', 'ъ'],
  ['ф', 'ы', 'в', 'а', 'п', 'р', 'о', 'л', 'д', 'ж', 'э'],
  ['я', 'ч', 'с', 'м', 'и', 'т', 'ь', 'б', 'ю'],
];
const KB_EN = [
  ['q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'],
  ['a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l'],
  ['z', 'x', 'c', 'v', 'b', 'n', 'm'],
];

const SoftKeyboard = ({ onKey, onBack, onEnter }) => {
  const [shift, setShift] = useState(false);
  const [ru, setRu] = useState(true);
  const rows = ru ? KB_RU : KB_EN;
  const tap = (c) => { onKey(shift ? c.toUpperCase() : c); if (shift) setShift(false); };
  return (
    <div className="mx-1 mb-1 shrink-0 rounded-2xl border border-white/[0.08] bg-[rgba(10,14,26,0.55)] p-1.5 backdrop-blur-xl">
      {rows.map((row, i) => (
        <div key={i} className="mb-1.5 flex gap-1" style={{ padding: '0 ' + (i === 1 ? 18 : 4) + 'px' }}>
          {row.map(c => (
            <button
              key={c} onClick={() => tap(c)}
              className="flex h-9 flex-1 items-center justify-center rounded-md bg-white/[0.07] text-[13px] text-white/85 transition active:scale-90 active:bg-white/20"
            >{shift ? c.toUpperCase() : c}</button>
          ))}
        </div>
      ))}
      <div className="mb-1.5 flex gap-1 px-1">
        <button onClick={() => tap('.')} className="h-9 w-10 shrink-0 rounded-md bg-white/[0.07] text-[13px] text-white/70 transition active:scale-90">.</button>
        <button onClick={() => tap(',')} className="h-9 w-10 shrink-0 rounded-md bg-white/[0.07] text-[13px] text-white/70 transition active:scale-90">,</button>
        <button onClick={() => onKey(' ')} className="h-9 flex-1 rounded-md bg-white/[0.07] text-[10px] text-white/50 transition active:scale-95">пробел</button>
        <button onClick={onBack} className="h-9 w-11 shrink-0 rounded-md bg-white/[0.07] text-[13px] text-white/60 transition active:scale-90">⌫</button>
        <button onClick={onEnter} className="h-9 w-11 shrink-0 rounded-md bg-accent/80 text-[13px] text-white transition active:scale-90">⏎</button>
      </div>
      <div className="flex gap-1 px-1 pb-0.5">
        <button
          onClick={() => setRu(v => !v)}
          className="h-8 w-14 shrink-0 rounded-md bg-white/[0.07] mono text-[11px] text-accent transition active:scale-90"
        >{ru ? 'En' : 'Ру'}</button>
        <button
          onClick={() => setShift(s => !s)}
          className={`h-8 w-12 shrink-0 rounded-md text-[13px] transition active:scale-90 ${shift ? 'bg-accent text-white' : 'bg-white/[0.07] text-white/60'}`}
        >⇧</button>
        <span className="mono flex flex-1 items-center justify-center text-[9px] text-white/20">KengaOS</span>
      </div>
    </div>
  );
};

/* ---------- браузер: kenga:// + внешние сайты (iframe) ---------- */

const BrowserAppM = () => {
  const [hist, setHist] = useState([HOME]);
  const [hidx, setHidx] = useState(0);
  const [draft, setDraft] = useState(null);
  const [nonce, setNonce] = useState(0);
  const url = hist[hidx];
  const page = BROWSER_PAGES[url];
  const external = !page && url.startsWith('http');

  const go = (raw) => {
    const t = parseAddr(raw);
    if (!t) { setDraft(null); return; }
    const nh = hist.slice(0, hidx + 1).concat(t);
    setHist(nh); setHidx(nh.length - 1); setDraft(null);
  };

  return (
    <div className="flex h-full flex-col">
      <div className="flex shrink-0 items-center gap-1.5 px-3 pt-2">
        <button
          onClick={() => setHidx(i => Math.max(0, i - 1))} disabled={hidx === 0}
          className={`glass flex h-8 w-8 shrink-0 items-center justify-center rounded-full text-[13px] ${hidx === 0 ? 'text-white/25' : 'text-white/75 active:scale-90'}`}
          aria-label="назад"
        >←</button>
        <button onClick={() => go(HOME)} className="glass flex h-8 w-8 shrink-0 items-center justify-center rounded-full text-[13px] text-white/75 active:scale-90" aria-label="главная">⌂</button>
        <button onClick={() => setDraft(url)} className="glass flex flex-1 items-center gap-2 rounded-full px-3.5 py-2 text-left">
          <span className="text-[12px] text-accent2">⌕</span>
          <span className={`flex-1 truncate text-[11px] ${draft !== null ? 'text-accent' : 'text-white/80'}`}>{draft !== null ? draft : url}</span>
        </button>
        <button onClick={() => setNonce(n => n + 1)} className="glass flex h-8 w-8 shrink-0 items-center justify-center rounded-full text-[13px] text-white/75 active:scale-90" aria-label="обновить">⟳</button>
      </div>
      {draft !== null ? (
        <SoftKeyboard
          onKey={(c) => setDraft(d => d + c)}
          onBack={() => setDraft(d => d.slice(0, -1))}
          onEnter={() => go(draft)}
        />
      ) : page ? (
        <div className="flex-1 overflow-auto px-5 pb-4">
          <div className="disp pt-3 text-lg text-white/90">{page.title}</div>
          <p className="mt-2 text-[12px] leading-relaxed text-white/55">{page.text}</p>
          <div className="mt-4 space-y-2">
            {page.links.map(([u, label]) => (
              <button
                key={u} onClick={() => go(u)}
                className="glass flex w-full items-center rounded-xl px-4 py-3 text-left text-[12px] text-accent transition active:scale-[0.98]"
              >
                {label}<span className="mono ml-auto text-[9px] text-white/30">{u}</span>
              </button>
            ))}
          </div>
        </div>
      ) : external ? (
        <div className="flex min-h-0 flex-1 flex-col">
          <iframe
            key={url + nonce}
            src={url}
            title="KengaOS browser"
            className="mx-3 mb-2 mt-2 min-h-0 flex-1 rounded-2xl border border-white/[0.08] bg-white"
          />
          <button
            onClick={() => window.open(url, '_blank')}
            className="mono mx-3 mb-2 shrink-0 rounded-xl bg-white/[0.06] py-1.5 text-[10px] text-white/50"
          >
            не открылся? сайт запретил встраивание → ↗ открыть снаружи
          </button>
        </div>
      ) : (
        <div className="flex flex-1 flex-col items-center justify-center gap-2 px-8 text-center">
          <span className="h-8 w-8 text-accent/50"><IconBrowser /></span>
          <div className="mono text-[10px] text-white/40">не похоже на адрес: {url}</div>
          <div className="text-[10px] leading-relaxed text-white/30">
            внутренние: kenga://home · внешние: example.com
          </div>
          <button onClick={() => go(HOME)} className="glass mt-2 rounded-lg px-3 py-1.5 text-[11px] text-accent">на главную</button>
        </div>
      )}
    </div>
  );
};

/* ---------- файлы: VFS с Kenga-исходниками ---------- */

const KENGA_HELLO = `// hello.kenga — первая программа на Кенга
on "start" {
    print("Привет из KengaOS!");
}

let имя: str = "мир";
print("Привет, " + имя);`;

const KENGA_PROPHET = `// prophet.kenga — пророк помнит и предсказывает
let p = memory(8);          // память паттернов

on "tick" {
    learn(p, state());
    let что_дальше = foresee(p);
    if surprise(p) > 0.5 {
        print("аномалия: " + что_дальше);
    }
}`;

const KENGA_AGENT = `// agent.kenga — агент с правами
agent Помощник {
    cap: [CAP_UI, CAP_IPC];
    on message m {
        reply("принял: " + m);
    }
}`;

const VFS = {
  '/': [
    ['bin', 'd'], ['etc', 'd'], ['home', 'd'],
    ['readme.txt', 'KengaOS v0.7 — ОС на языке Кенга.\nОдно ядро для ПК и телефона.\n\nЭтот VFS — как initrd в ядре: файлы внутри оболочки.'],
  ],
  '/bin': [
    ['shell', 'KengaOS shell · UTF-8\nкоманды: help, uptime, mem, agents, ls, cat'],
    ['kenga-run', 'рантайм Kenga (bytecode)\nиспользование: kenga-run <файл.kenga>'],
  ],
  '/etc': [
    ['motd', 'Добро пожаловать в KengaOS.\nПророки уже наблюдают.'],
    ['agents.conf', 'ui-agent    CAP_UI\nmodel-agent CAP_MODEL_INFER\nkenga-agent CAP_IPC, CAP_UI\nvfs         CAP_FS'],
  ],
  '/home': [['user', 'd']],
  '/home/user': [
    ['hello.kenga', KENGA_HELLO],
    ['prophet.kenga', KENGA_PROPHET],
    ['agent.kenga', KENGA_AGENT],
    ['mind.km', 'KENGAMIND\x00v1 · паттерны пророка · 8 dims · эпизоды: 32'],
  ],
};

const FilesLite = () => {
  const [path, setPath] = useState('/');
  const [file, setFile] = useState(null); // [имя, содержимое]
  if (file) {
    return (
      <div className="flex h-full flex-col">
        <div className="mono flex shrink-0 items-center gap-2 px-4 pt-3 text-[11px] text-accent2">
          <button onClick={() => setFile(null)} className="text-white/50">←</button>
          {file[0]}
        </div>
        <pre className="mono flex-1 overflow-auto whitespace-pre-wrap px-4 py-3 text-[11px] leading-relaxed text-white/75">{file[1]}</pre>
      </div>
    );
  }
  const items = VFS[path] || [];
  return (
    <div className="flex h-full flex-col">
      <div className="mono flex shrink-0 items-center gap-1 px-4 pt-3 text-[11px]">
        {path !== '/' && <button onClick={() => setPath(p => p.slice(0, p.lastIndexOf('/')) || '/')} className="text-white/50">←</button>}
        <span className="text-accent">vfs:</span>
        <span className="text-white/70">{path}</span>
      </div>
      <div className="flex-1 space-y-1.5 overflow-auto px-3 pt-3 pb-3">
        {items.map(([name, kind]) => (
          <button
            key={name}
            onClick={() => kind === 'd' ? setPath(p => (p === '/' ? '' : p) + '/' + name) : setFile([name, kind])}
            className="glass flex w-full items-center gap-3 rounded-xl px-4 py-2.5 text-left transition active:scale-[0.98]"
          >
            <span className={`text-[13px] ${kind === 'd' ? 'text-accent2' : 'text-white/35'}`}>{kind === 'd' ? '▸' : '≡'}</span>
            <span className="flex-1 truncate text-[12px] text-white/85">{name}</span>
            {kind === 'd' && <span className="mono text-[9px] text-white/25">папка</span>}
          </button>
        ))}
      </div>
    </div>
  );
};

/* ---------- агенты: живой чат с системными агентами ---------- */

const AGENT_PROFILES = [
  { id: 'ui-agent', name: 'UI-агент', tag: 'CAP_UI', color: 'text-accent',
    greet: 'Оболочка на связи. Отрисовываю стекло, слушаю события.',
    replies: ['Принял, отрисую в следующем кадре.', 'Понял. Поведение записываю в память сессии.', 'Событие доставлено ядру.'] },
  { id: 'model-agent', name: 'Модель-агент', tag: 'CAP_MODEL_INFER', color: 'text-accent2',
    greet: 'MLP онлайн. Дай два числа через пробел — предскажу выход.',
    replies: ['Считаю… готово: см. прогноз в логе.', 'Паттерн похож на виденные — удивление низкое.', 'Обучусь на этом примере при следующем тике.'] },
  { id: 'kenga-agent', name: 'Кенга-агент', tag: 'CAP_IPC', color: 'text-accent',
    greet: 'Я написан на Кенге. Спроси про язык: memory, learn, foresee.',
    replies: ['В Кенге это делается так: learn(p, state()).', 'Пророки — часть ядра, а не приложение.', 'Пиши код — компилятор emit-c уже собран.'] },
  { id: 'vfs', name: 'VFS', tag: 'CAP_FS', color: 'text-accent2',
    greet: 'Initrd смонтирован: /bin, /etc, /home/user.',
    replies: ['Файл найден в дереве VFS.', 'Права есть: чтение разрешено.', 'Записи пока нет — ядро live.']},
];

const AgentsLite = () => {
  const [active, setActive] = useState(AGENT_PROFILES[0]);
  const [chats, setChats] = useState({});   // id: [{me:bool, text}]
  const [cur, setCur] = useState('');
  const [seen, setSeen] = useState({});     // id: true — прочитан грит
  const log = useRef(null);
  const msgs = chats[active.id] || [];

  useEffect(() => {
    if (!chats[active.id] && !seen[active.id]) {
      setSeen(s => ({ ...s, [active.id]: true }));
      setChats(c => ({ ...c, [active.id]: [{ me: false, text: active.greet }] }));
    }
  }, [active.id]);

  useEffect(() => { if (log.current) log.current.scrollTop = log.current.scrollHeight; }, [chats, active.id]);

  const send = () => {
    const t = cur.trim();
    if (!t) return;
    setCur('');
    setChats(c => ({ ...c, [active.id]: [...(c[active.id] || []), { me: true, text: t }] }));
    setTimeout(() => {
      const r = active.replies[Math.floor(Math.random() * active.replies.length)];
      setChats(c => ({ ...c, [active.id]: [...(c[active.id] || []), { me: false, text: r }] }));
    }, 500);
  };

  return (
    <div className="flex h-full flex-col">
      <div className="flex shrink-0 gap-1.5 overflow-x-auto px-3 pt-2">
        {AGENT_PROFILES.map(a => (
          <button
            key={a.id} onClick={() => setActive(a)}
            className={`glass shrink-0 rounded-full px-3 py-1.5 text-[10px] transition active:scale-95 ${a.id === active.id ? 'border-accent/50 text-white' : 'text-white/50'}`}
          >
            {a.name}<span className={`mono ml-1 text-[8px] ${a.color}`}>●</span>
          </button>
        ))}
      </div>
      <div ref={log} className="flex-1 space-y-2 overflow-auto px-3 py-3">
        {msgs.map((m, i) => (
          <div key={i} className={`flex ${m.me ? 'justify-end' : 'justify-start'}`}>
            <div className={`max-w-[80%] rounded-2xl px-3.5 py-2 text-[12px] leading-snug ${m.me ? 'bg-accent/25 text-white' : 'glass text-white/80'}`}>
              {m.text}
            </div>
          </div>
        ))}
      </div>
      <div className="glass mx-3 mb-1 flex shrink-0 items-center rounded-lg px-3 py-1.5">
        <span className="mono flex-1 truncate text-[11px] text-white/85">{cur}<span className="animate-blink">▌</span></span>
        <button onClick={send} className="text-[12px] text-accent">отправить</button>
      </div>
      <SoftKeyboard
        onKey={(ch) => setCur(s => s + ch)}
        onBack={() => setCur(s => s.slice(0, -1))}
        onEnter={send}
      />
    </div>
  );
};

/* ---------- телефон: набор номера ---------- */

const DIAL_KEYS = [
  ['1', ''], ['2', 'ABC'], ['3', 'DEF'],
  ['4', 'GHI'], ['5', 'JKL'], ['6', 'MNO'],
  ['7', 'PQRS'], ['8', 'TUV'], ['9', 'WXYZ'],
  ['*', ''], ['0', '+'], ['#', ''],
];
let RECENTS = [];   /* журнал за сессию */

/* T9: цифра → буквы (латиница + кириллица) */
const T9 = {
  2: 'abcабвг', 3: 'defдежз', 4: 'ghiийкл', 5: 'jklмноп',
  6: 'mnoрсту', 7: 'pqrsфхцч', 8: 'tuvшщъы', 9: 'wxyzьэюя',
};
const nameToDigits = (name) => name.toLowerCase().split('').map(ch => {
  if (/[0-9]/.test(ch)) return ch;
  for (const [d, letters] of Object.entries(T9)) if (letters.includes(ch)) return d;
  return '';
}).join('');

const DialerApp = () => {
  const [num, setNum] = useState(MOBILE_QS.get('num') || '');
  const [call, setCall] = useState(null);
  const [sec, setSec] = useState(0);
  const [speaker, setSpeaker] = useState(false);
  const [recent, setRecent] = useState(RECENTS);
  const [contacts, setContacts] = useState([]);
  const native = typeof window !== 'undefined' ? window.KengaNative : null;

  /* реальные контакты из телефона (мост из MainActivity) */
  useEffect(() => {
    try {
      const list = JSON.parse(native && native.contacts ? native.contacts() : '[]');
      setContacts(Array.isArray(list) ? list : []);
    } catch { setContacts([]); }
  }, []);

  useEffect(() => {
    if (!call) return;
    const t = setInterval(() => setSec(s => s + 1), 1000);
    return () => clearInterval(t);
  }, [call]);

  const digit = (d) => { if (num.length < 16) setNum(num + d); };
  const doCall = () => {
    if (!num) return;
    RECENTS = [num, ...RECENTS.filter(r => r !== num)].slice(0, 6);
    setRecent(RECENTS);
    if (native && native.dial) { native.dial(num); return; }  // реальный вызов
    setSec(0); setCall(num);                                   // демо-вызов (десктоп)
  };
  const mmss = (s) => Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');

  if (call) {
    return (
      <div className="flex h-full flex-col items-center justify-center gap-5">
        <div className="flex h-20 w-20 items-center justify-center rounded-full bg-accent/20 text-2xl text-accent">✆</div>
        <div className="disp text-2xl text-white/90">{call}</div>
        <div className="mono text-[12px] text-white/45">{sec < 3 ? 'вызов…' : mmss(sec)}</div>
        <div className="flex items-center gap-3">
          <button
            onClick={() => setSpeaker(sp => !sp)}
            className={`flex h-11 w-11 items-center justify-center rounded-full text-[14px] transition active:scale-90 ${speaker ? 'bg-accent text-white' : 'glass text-white/60'}`}
            aria-label="громкая связь"
          >🔈</button>
          <button
            onClick={() => setCall(null)}
            className="flex h-14 w-14 items-center justify-center rounded-full bg-red-500/90 text-lg text-white transition active:scale-90"
            aria-label="завершить вызов"
          >✕</button>
        </div>
        {speaker && <div className="mono text-[10px] text-accent2">громкая связь</div>}
      </div>
    );
  }

  const pat = nameToDigits(num.replace(/[^0-9*#]/g, ''));
  const matched = num
    ? contacts.filter(c =>
        c.t.replace(/[^0-9]/g, '').includes(num.replace(/[^0-9]/g, '')) ||
        (pat && nameToDigits(c.n).includes(pat)))
    : contacts;

  return (
    <div className="flex h-full flex-col px-5 pb-3">
      <div className="disp mt-2 flex min-h-[40px] items-center truncate text-2xl font-light tracking-wide text-white/95">
        {num || <span className="text-white/25">номер</span>}
      </div>
      {/* контакты: совпадения по цифрам/буквам, либо весь список */}
      <div className="mt-1 min-h-0 flex-1 space-y-1 overflow-y-auto">
        {num && recent.length > 0 && (
          <div className="flex flex-wrap gap-1.5 pb-1">
            {recent.map(r => (
              <button key={r} onClick={() => setNum(r)} className="mono rounded-full bg-white/[0.06] px-2.5 py-0.5 text-[10px] text-white/50">{r}</button>
            ))}
          </div>
        )}
        {matched.map((c, i) => (
          <button
            key={c.t + i} onClick={() => setNum(c.t)}
            className="glass flex w-full items-center gap-3 rounded-xl px-3 py-2 text-left transition active:scale-[0.98]"
          >
            <span className="flex h-8 w-8 items-center justify-center rounded-full bg-accent/15 text-[11px] text-accent">{c.n[0]}</span>
            <span className="flex-1 truncate text-[12px] text-white/85">{c.n}</span>
            <span className="mono text-[10px] text-white/40">{c.t}</span>
          </button>
        ))}
        {contacts.length === 0 && (
          <div className="mono px-1 pt-2 text-[10px] leading-relaxed text-white/25">
            {native ? 'контакты недоступны — дай разрешение' : 'контакты: только в APK на телефоне'}
          </div>
        )}
      </div>
      <div className="mx-auto mt-2 grid w-full max-w-[300px] grid-cols-3 gap-2">
        {DIAL_KEYS.map(([d, sub]) => (
          <button key={d} onClick={() => digit(d)} className="glass flex flex-col items-center rounded-2xl py-2.5 transition active:scale-90">
            <span className="text-xl text-white/90">{d}</span>
            {sub && <span className="mono text-[7px] tracking-[2px] text-white/35">{sub}</span>}
          </button>
        ))}
      </div>
      <div className="mx-auto mt-3 flex w-full max-w-[300px] items-center justify-between">
        <span className="w-14" />
        <button
          onClick={doCall}
          className={`flex h-14 w-14 items-center justify-center rounded-full text-xl transition active:scale-90 ${num ? 'bg-accent text-white' : 'bg-white/[0.08] text-white/30'}`}
          aria-label="вызов"
        >✆</button>
        <button
          onClick={() => setNum(n => n.slice(0, -1))}
          className={`w-14 text-left text-lg text-white/50 transition active:scale-90 ${num ? '' : 'opacity-0'}`}
          aria-label="стереть"
        >⌫</button>
      </div>
    </div>
  );
};

/* ---------- терминал (мобильный): экранная клавиатура + консоль ---------- */

/* алиасы: и латиница, и кириллица ведут к одной команде */
const TERM_ALIASES = {
  help: ['help', 'помощь', '?'],
  uptime: ['uptime', 'аптайм', 'время'],
  mem: ['mem', 'meminfo', 'память'],
  agents: ['agents', 'агенты', 'ps'],
  echo: ['echo', 'эхо'],
  clear: ['clear', 'очистить', 'cls'],
};

const TermLite = () => {
  const [lines, setLines] = useState([
    'KengaOS mobile shell · kernel bridge (симуляция)',
    'команды: help · uptime · mem · agents · echo · clear',
    '(можно по-русски: помощь · аптайм · память · агенты)',
  ]);
  const [cur, setCur] = useState('');
  const log = useRef(null);
  useEffect(() => { if (log.current) log.current.scrollTop = log.current.scrollHeight; }, [lines]);
  const run = (raw) => {
    const parts = raw.trim().split(/\s+/);
    const c = (parts[0] || '').toLowerCase();
    if (!c) return;
    if (TERM_ALIASES.clear.some(a => a === c)) { setLines([]); return; }
    const canon = Object.keys(TERM_ALIASES).find(k => TERM_ALIASES[k].some(a => a === c));
    let out;
    if (canon === 'help') out = 'help · uptime · mem · agents · echo <текст> · clear';
    else if (canon === 'uptime') out = 'uptime: ' + fmt(Math.floor(performance.now() / 1000));
    else if (canon === 'mem') out = 'ram 41% · kernel bridge (симуляция; реальное ядро — в QEMU)';
    else if (canon === 'agents') out = 'агенты: ui-agent, model-agent, kenga-agent, vfs';
    else if (canon === 'echo') out = parts.slice(1).join(' ') || '';
    else out = 'неизвестная команда: ' + c + '  (help — список)';
    setLines(ls => [...ls, '» ' + raw, out]);
  };
  return (
    <div className="flex h-full flex-col">
      <div ref={log} className="mono flex-1 overflow-auto px-4 py-3 text-[11px] leading-relaxed text-white/70">
        {lines.map((l, i) => (
          <div key={i} className={l.startsWith('»') ? 'text-accent2' : ''}>{l}</div>
        ))}
      </div>
      <div className="glass mx-3 mb-1 flex shrink-0 items-center rounded-lg px-3 py-1.5">
        <span className="mono text-[11px] text-accent">»</span>
        <span className="mono flex-1 truncate pl-2 text-[11px] text-white/85">{cur}<span className="animate-blink">▌</span></span>
      </div>
      <SoftKeyboard
        onKey={(ch) => setCur(s => s + ch)}
        onBack={() => setCur(s => s.slice(0, -1))}
        onEnter={() => { if (cur.trim()) run(cur); setCur(''); }}
      />
    </div>
  );
};

/* ---------- приложение на весь экран (без светофора) ---------- */

const AppSheet = ({ app, onClose }) => {
  const icon = typeof app.icon === 'string' ? ICONS[app.icon] : app.icon;
  return (
    <div className="animate-pop-in absolute inset-0 z-40 flex flex-col">
      <div className="glass m-2 flex flex-1 flex-col overflow-hidden rounded-3xl">
        <div className="flex items-center gap-3 border-b border-white/[0.06] px-4 py-3">
          <button
            onClick={onClose}
            className="flex h-8 w-8 items-center justify-center rounded-full bg-white/[0.06] text-white/70 transition hover:bg-white/[0.12] active:scale-90"
            aria-label="назад"
          >
            <span className="h-4 w-4"><IconBack /></span>
          </button>
          <span className="flex items-center gap-2 text-sm text-white/85">
            <span className="h-4 w-4 text-accent">{icon}</span>
            {app.name}
          </span>
          <span className="badge ml-auto hidden px-1.5 py-0.5 text-[9px] sm:block">{app.tag}</span>
        </div>
        <div className="min-h-0 flex-1 overflow-hidden">
          {app.id === 'terminal' ? <TermLite />
            : app.id === 'browser' ? <BrowserAppM />
            : app.id === 'phone' ? <DialerApp />
            : app.id === 'agents' ? <AgentsLite />
            : app.id === 'files' ? <FilesLite />
            : <div className="flex h-full flex-col items-center justify-center gap-3 text-white/35">
                <span className="h-10 w-10 text-accent/60">{icon}</span>
                <span className="mono text-[11px]">модуль «{app.name}» · этап порта в Kenga</span>
              </div>}
        </div>
      </div>
    </div>
  );
};

/* ---------- домашний экран ---------- */

const Home = ({ k, onOpen, onLauncher, onShade }) => {
  const dock = PHONE_DOCK;
  return (
    <div className="animate-fade-in absolute inset-0 flex flex-col">
      <StatusBar k={k} onShade={onShade} />
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
        <div className="glass flex items-center justify-around rounded-3xl px-3 py-3.5">
          {dock.map(a => (
            <button
              key={a.id}
              onClick={() => onOpen({ id: a.id, name: a.name, icon: a.icon, tag: 'v1' })}
              className="flex flex-col items-center gap-1 text-accent transition active:scale-90"
              aria-label={a.name}
            >
              <span className="h-6 w-6">{a.icon}</span>
              <span className="text-[8px] text-white/45">{a.name}</span>
            </button>
          ))}
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
  const skip = MOBILE_QS.has('skip');
  const [booted, setBooted] = useState(skip);
  const [locked, setLocked] = useState(!skip);
  const [launcher, setLauncher] = useState(false);
  const [openApp, setOpenApp] = useState(() => {
    const id = MOBILE_QS.get('app');
    if (!id) return null;
    const src = APPS.find(a => a.id === id) || PHONE_DOCK.find(a => a.id === id);
    return src ? { id: src.id, name: src.name, icon: src.icon, tag: src.tag || 'v1' } : null;
  });
  const [shade, setShade] = useState(MOBILE_QS.has('shade'));
  const k = useKernel(booted);

  const open = (app) => { setLauncher(false); setShade(false); setOpenApp(app); };
  const home = () => { setOpenApp(null); setLauncher(false); setShade(false); };

  return (
    <div className="relative mx-auto h-screen max-w-[430px] overflow-hidden">
      <Wallpaper />
      {booted && (
        <>
          {locked
            ? <LockScreen k={k} onUnlock={() => setLocked(false)} />
            : <Home k={k} onOpen={open} onLauncher={() => setLauncher(true)} onShade={() => setShade(true)} />}
          {launcher && <Launcher onOpen={open} onClose={home} />}
          {openApp && <AppSheet app={openApp} onClose={home} />}
          {shade && <Shade open={shade} onClose={() => setShade(false)} />}
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
