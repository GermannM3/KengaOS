import React, { useState } from 'react';

/* Браузер KengaOS v2 — UI-прототип оболочки.

   Внутренние страницы kenga:// рендерим сами; внешние сайты —
   через iframe оболочки (оболочка пока живёт внутри браузера
   хоста, так что это честно работающая навигация). Сайты с
   X-Frame-Options: DENY не встроятся — для них кнопка «открыть
   снаружи». В будущей ОС это заменит наш движок / порт Servo.
   Мобильная версия (Mobile.jsx) использует BROWSER_PAGES и
   parseAddr отсюда. */

export const BROWSER_PAGES = {
  'kenga://home': {
    title: 'Внутренняя сеть',
    text: 'Страницы оболочки KengaOS. Внешние сайты открываются в окне ниже; часть сайтов запрещает встраивание.',
    links: [['kenga://about', 'О системе'], ['kenga://prophets', 'Пророки'], ['kenga://sys', 'Ядро'], ['https://example.com', 'example.com (тест)']],
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

export const HOME = 'kenga://home';

/* адрес → цель: внутренняя | https | null (не похоже на адрес) */
export const parseAddr = (raw) => {
  const u = (raw || '').trim();
  if (!u) return HOME;
  if (u.startsWith('kenga://')) return BROWSER_PAGES[u] ? u : null;
  if (/^https?:\/\//i.test(u)) return u;
  if (/^[\w-]+(\.[\w-]+)+/.test(u)) return 'https://' + u;
  return null;
};

const IconBtn = ({ children, onClick, title, disabled }) => (
  <button
    onClick={onClick} title={title} disabled={disabled}
    className={`flex h-7 w-7 shrink-0 items-center justify-center rounded-lg text-[13px] transition active:scale-90
      ${disabled ? 'text-white/20' : 'text-white/70 hover:bg-white/[0.08] hover:text-white'}`}
  >{children}</button>
);

/* Десктопная версия: железная клавиатура. */
const BrowserApp = () => {
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
  const jump = (d) => {
    const ni = Math.max(0, Math.min(hist.length - 1, hidx + d));
    setHidx(ni); setDraft(null);
  };

  return (
    <div className="flex h-full flex-col">
      <div className="flex shrink-0 items-center gap-1 px-3 pt-3">
        <IconBtn onClick={() => jump(-1)} disabled={hidx === 0} title="Назад">←</IconBtn>
        <IconBtn onClick={() => jump(1)} disabled={hidx >= hist.length - 1} title="Вперёд">→</IconBtn>
        <IconBtn onClick={() => setNonce(n => n + 1)} title="Обновить">⟳</IconBtn>
        <IconBtn onClick={() => go(HOME)} title="Главная">⌂</IconBtn>
        <input
          value={draft !== null ? draft : url}
          onChange={(e) => setDraft(e.target.value)}
          onKeyDown={(e) => { if (e.key === 'Enter') go(draft); }}
          onBlur={() => setDraft(null)}
          spellCheck="false"
          className="field mx-1 flex-1 rounded-lg px-3 py-1.5 text-[12px] outline-none"
          placeholder="kenga://… или адрес сайта"
        />
        {external && (
          <IconBtn onClick={() => window.open(url, '_blank')} title="Открыть в новом окне (если сайт запретил встраивание)">↗</IconBtn>
        )}
      </div>
      {page ? (
        <div className="flex-1 overflow-auto px-6 pb-5">
          <div className="disp pt-4 text-xl text-white/90">{page.title}</div>
          <p className="mt-2 max-w-[52ch] text-[13px] leading-relaxed text-white/55">{page.text}</p>
          <div className="mt-5 flex max-w-[420px] flex-col gap-2">
            {page.links.map(([u, label]) => (
              <button
                key={u} onClick={() => go(u)}
                className="glass flex w-full items-center rounded-xl px-4 py-2.5 text-left text-[13px] text-accent transition hover:bg-white/[0.08]"
              >
                {label}<span className="mono ml-auto text-[9px] text-white/30">{u}</span>
              </button>
            ))}
          </div>
        </div>
      ) : external ? (
        <iframe
          key={url + nonce}
          src={url}
          title="KengaOS browser"
          className="mx-3 mb-3 mt-2 min-h-0 flex-1 rounded-xl border border-white/[0.08] bg-white"
        />
      ) : (
        <div className="flex flex-1 flex-col items-center justify-center gap-2 text-center">
          <div className="mono text-[11px] text-white/40">не похоже на адрес: {url}</div>
          <div className="max-w-[40ch] text-[11px] leading-relaxed text-white/30">
            внутренние страницы: kenga://home · внешние: адрес вида example.com
          </div>
          <button onClick={() => go(HOME)} className="glass mt-2 rounded-lg px-3 py-1.5 text-[12px] text-accent">на главную</button>
        </div>
      )}
    </div>
  );
};

export default BrowserApp;
