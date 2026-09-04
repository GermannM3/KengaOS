import React, { useState } from 'react';

/* Браузер KengaOS v1 — внутренние страницы kenga://.

   ponytail: настоящий движок веб-страниц — годы работы; v1 честно
   показывает только внутренние страницы оболочки. Внешние сайты —
   после порта сетевого стека в ядро и/или порта готового движка
   (Servo/WebKit-класс) в userspace KengaOS.
   Используется десктопом (input + Enter) и мобилкой (экранная
   клавиатура в Mobile.jsx импортирует BROWSER_PAGES). */

export const BROWSER_PAGES = {
  'kenga://home': {
    title: 'Внутренняя сеть',
    text: 'Страницы оболочки KengaOS. Внешние сайты откроются после порта сетевого стека в ядро.',
    links: [['kenga://about', 'О системе'], ['kenga://prophets', 'Пророки'], ['kenga://sys', 'Ядро']],
  },
  'kenga://about': {
    title: 'KengaOS',
    text: 'Операционная система на языке Кенга: одно ядро для ПК и телефона, стеклянный UI, агенты и пророки как часть ядра.',
    links: [['kenga://home', '← на главную']],
  },
  'kenga://prophets': {
    title: 'Пророки',
    text: 'Пророк — паттерновая память ядра: предсказывает следующее действие, удивляется аномалиям, «спит» и консолидирует опыт. API: memory / learn / predict / foresee.',
    links: [['kenga://home', '← на главную']],
  },
  'kenga://sys': {
    title: 'Ядро',
    text: 'Это UI-прототип (kernel bridge — симуляция). Реальное ядро: kmain.kenga → emit-c → x86_64 + aarch64, SMOKE OK в QEMU.',
    links: [['kenga://home', '← на главную']],
  },
};

const IconGlobe = () => (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" className="h-4 w-4 text-accent/60">
    <circle cx="12" cy="12" r="9" />
    <path d="M3 12h18M12 3a14 14 0 0 1 0 18a14 14 0 0 1 0-18z" />
  </svg>
);

/* Десктопная версия: обычный input (железная клавиатура).
   Пропсом addr можно задать внешний режим набора (мобилка). */
const BrowserApp = ({ url: urlProp, onAddr }) => {
  const [url, setUrl] = useState(urlProp || 'kenga://home');
  const [draft, setDraft] = useState(null); // null = не редактируем
  const page = BROWSER_PAGES[url];
  const value = draft !== null ? draft : url;
  const commit = () => {
    const v = (draft || '').trim();
    setUrl(BROWSER_PAGES[v] ? v : url);
    if (onAddr) onAddr(v);
    setDraft(null);
  };
  return (
    <div className="flex h-full flex-col">
      <div className="flex shrink-0 items-center gap-2 px-3 pt-3">
        <span className="text-accent2">⌕</span>
        <input
          value={value}
          onChange={(e) => setDraft(e.target.value)}
          onKeyDown={(e) => { if (e.key === 'Enter') commit(); }}
          onBlur={commit}
          spellCheck="false"
          className="field flex-1 rounded-lg px-3 py-1.5 text-[12px] outline-none"
          placeholder="kenga://…"
        />
      </div>
      {page ? (
        <div className="flex-1 overflow-auto px-6 pb-5">
          <div className="disp pt-4 text-xl text-white/90">{page.title}</div>
          <p className="mt-2 max-w-[52ch] text-[13px] leading-relaxed text-white/55">{page.text}</p>
          <div className="mt-5 flex max-w-[420px] flex-col gap-2">
            {page.links.map(([u, label]) => (
              <button
                key={u} onClick={() => { setUrl(u); setDraft(null); }}
                className="glass flex w-full items-center rounded-xl px-4 py-2.5 text-left text-[13px] text-accent transition hover:bg-white/[0.08]"
              >
                {label}<span className="mono ml-auto text-[9px] text-white/30">{u}</span>
              </button>
            ))}
          </div>
        </div>
      ) : (
        <div className="flex flex-1 flex-col items-center justify-center gap-2 text-center">
          <IconGlobe />
          <div className="mono text-[11px] text-white/40">страница не найдена: {url}</div>
          <div className="max-w-[40ch] text-[11px] leading-relaxed text-white/30">
            внешние сайты — после сетевого стека в ядре. внутренние: kenga://home
          </div>
          <button onClick={() => setUrl('kenga://home')} className="glass mt-2 rounded-lg px-3 py-1.5 text-[12px] text-accent">на главную</button>
        </div>
      )}
    </div>
  );
};

export default BrowserApp;
