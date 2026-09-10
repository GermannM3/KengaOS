import React, { useState, useEffect, useRef } from 'react';
import { getTheme, setTheme } from '../theme.js';

/* Общие приложения KengaOS — используются десктопом и мобилкой.
   Приложения с вводом принимают prop softKeyboard:
   true — экранная клавиатура (мобилка), false — input (десктоп). */

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

export const SoftKeyboard = ({ onKey, onBack, onEnter }) => {
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

/* ---------- монитор: телеметрия ядра ---------- */

const Spark = ({ data, color }) => {
  const n = data.length;
  const pts = n
    ? data.map((v, i) => `${(i * 100) / Math.max(1, n - 1)},${100 - Math.max(2, Math.min(100, v))}`).join(' ')
    : '0,100';
  return (
    <svg viewBox="0 0 100 100" preserveAspectRatio="none" className="h-16 w-full">
      <polyline points={`0,100 ${pts} 100,100`} fill={color} fillOpacity="0.12" stroke="none" />
      <polyline points={pts} fill="none" stroke={color} strokeWidth="2" vectorEffect="non-scaling-stroke" />
    </svg>
  );
};

export const MonitorApp = ({ cpu = 12, ram = 41, uptime = '—', ipc = [] }) => {
  const [hist, setHist] = useState({ cpu: [], ram: [] });
  useEffect(() => {
    setHist(h => ({
      cpu: [...h.cpu.slice(-39), Math.max(2, Math.min(100, cpu))],
      ram: [...h.ram.slice(-39), Math.max(2, Math.min(100, ram))],
    }));
  }, [cpu, ram]);
  const box = 'glass rounded-2xl p-4';
  return (
    <div className="h-full space-y-3 overflow-auto p-4">
      <div className={box}>
        <div className="mono flex items-baseline justify-between text-[10px] text-white/45">
          <span>CPU</span><span className="text-accent text-[15px] font-semibold">{Math.round(cpu)}%</span>
        </div>
        <Spark data={hist.cpu} color="var(--accent)" />
      </div>
      <div className={box}>
        <div className="mono flex items-baseline justify-between text-[10px] text-white/45">
          <span>RAM</span><span className="text-accent2 text-[15px] font-semibold">{Math.round(ram)}%</span>
        </div>
        <Spark data={hist.ram} color="var(--accent2)" />
      </div>
      <div className={`${box} mono flex items-center justify-between text-[10px] text-white/50`}>
        <span>uptime</span><span className="text-white/85">{uptime}</span>
      </div>
      {ipc.length > 0 && (
        <div className={box}>
          <div className="mono pb-2 text-[10px] tracking-wider text-white/45">ЖУРНАЛ IPC</div>
          <div className="mono space-y-1 text-[10px] text-white/50">
            {ipc.map(([a, b, m], i) => (
              <div key={i}><span className="text-accent2">[{a}]</span> → <span className="text-accent">{b}</span> · {m}</div>
            ))}
          </div>
        </div>
      )}
    </div>
  );
};

/* ---------- настройки: темы, обои, о системе ---------- */

const THEME_BG = {
  aurora: 'linear-gradient(135deg,#8b7bff,#22d3ee)',
  blue: 'linear-gradient(135deg,#3fa4ff,#2ee6c8)',
  green: 'linear-gradient(135deg,#2fe3a0,#49c9ff)',
};

export const ThemeDots = ({ size = 22 }) => {
  const [cur, setCur] = useState(getTheme());
  return (
    <div className="flex items-center gap-2.5">
      {[['aurora', 'Полярная'], ['blue', 'Синяя волна'], ['green', 'Зелёная волна']].map(([id, name]) => (
        <button
          key={id}
          onClick={() => { setTheme(id); setCur(id); }}
          title={name}
          className="rounded-full transition active:scale-90"
          style={{
            width: size, height: size, background: THEME_BG[id],
            border: cur === id ? '2px solid rgba(255,255,255,0.9)' : '1px solid rgba(255,255,255,0.25)',
          }}
        />
      ))}
    </div>
  );
};

export const SettingsApp = ({ wallpaper = false, wallIdx = 0, onWallIdx }) => (
  <div className="h-full space-y-3 overflow-auto p-4">
    <div className="glass flex items-center justify-between rounded-2xl px-4 py-3">
      <span className="text-[12px] text-white/80">Тема</span>
      <ThemeDots />
    </div>
    {wallpaper && (
      <div className="glass flex items-center justify-between rounded-2xl px-4 py-3">
        <span className="text-[12px] text-white/80">Обои</span>
        <div className="flex gap-2">
          {[0, 1].map(i => (
            <button
              key={i} onClick={() => onWallIdx && onWallIdx(i)}
              className={`h-8 w-12 rounded-lg transition active:scale-90 ${wallIdx === i ? 'border-accent' : ''}`}
              style={{
                border: wallIdx === i ? '2px solid var(--accent)' : '1px solid rgba(255,255,255,0.2)',
              }}
            >{i + 1}</button>
          ))}
        </div>
      </div>
    )}
    <div className="glass rounded-2xl px-4 py-3 mono text-[10px] leading-relaxed text-white/45">
      <div className="flex justify-between"><span>оболочка</span><span className="text-white/75">KengaOS 0.8</span></div>
      <div className="mt-1 flex justify-between"><span>мост ядра</span><span className="text-white/75">симуляция kd_*</span></div>
      <div className="mt-1 flex justify-between"><span>реальное ядро</span><span className="text-white/75">x86_64 + aarch64 · QEMU OK</span></div>
    </div>
  </div>
);

/* ---------- о системе ---------- */

export const AboutApp = () => (
  <div className="h-full space-y-3 overflow-auto p-5">
    <div className="flex items-center gap-3">
      <svg viewBox="0 0 48 48" width="42" height="42">
        <defs>
          <linearGradient id="lgAbout" x1="0" y1="0" x2="1" y2="1">
            <stop offset="0" stopColor="var(--accent)" />
            <stop offset="1" stopColor="var(--accent2)" />
          </linearGradient>
        </defs>
        <path d="M24 3 L41 12.5v19L24 41 7 31.5v-19z" fill="none" stroke="url(#lgAbout)" strokeWidth="3" />
        <path d="M18 15v18M18 24l12-9M18 24l12 9" stroke="url(#lgAbout)" strokeWidth="3" strokeLinecap="round" fill="none" />
      </svg>
      <div>
        <div className="disp text-lg text-white/95">KengaOS 0.8</div>
        <div className="mono text-[10px] text-white/40">agent-native · пророки в ядре</div>
      </div>
    </div>
    <p className="text-[12px] leading-relaxed text-white/55">
      Операционная система на языке Кенга: одно ядро для ПК и телефона,
      стеклянный UI, агенты с правами и пророки как часть ядра.
    </p>
    <div className="glass space-y-1.5 rounded-2xl px-4 py-3 mono text-[10px] leading-relaxed text-white/50">
      <div>✓ ядро x86_64 — ISO, QEMU SMOKE OK (CI)</div>
      <div>✓ ядро aarch64 — QEMU virt, тот же исходник (CI)</div>
      <div>✓ ring 3 · магазин .kpkg · пророк v1 · xHCI</div>
      <div>✓ оболочки: десктоп + мобилка (этап «оболочка»)</div>
      <div className="text-white/30">→ дальше: диск/запись, Wi-Fi, телефон fastboot boot</div>
    </div>
  </div>
);

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
    ['readme.txt', 'KengaOS v0.8 — ОС на языке Кенга.\nОдно ядро для ПК и телефона.\n\nЭтот VFS — как initrd в ядре: файлы внутри оболочки.'],
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

export const FilesApp = () => {
  const [path, setPath] = useState('/');
  const [file, setFile] = useState(null);
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

/* ---------- агенты: чат с системными агентами ---------- */

export const AGENT_PROFILES = [
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
    replies: ['Файл найден в дереве VFS.', 'Права есть: чтение разрешено.', 'Записи пока нет — ядро live.'] },
];

export const AgentsApp = ({ softKeyboard = false }) => {
  const [active, setActive] = useState(AGENT_PROFILES[0]);
  const [chats, setChats] = useState({});
  const [cur, setCur] = useState('');
  const [seen, setSeen] = useState({});
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
    /* модель-агент: «XOR-предсказание» как в ядре (MLP 2-2-1) */
    const nums = t.trim().split(/\s+/).map(Number);
    let reply = active.replies[Math.floor(Math.random() * active.replies.length)];
    if (active.id === 'model-agent' && nums.length === 2 && nums.every(n => n === 0 || n === 1)) {
      const xor = nums[0] ^ nums[1];
      reply = `вход [${nums[0]}, ${nums[1]}] → прогноз: ${xor} (MLP 2-2-1, ядро)`;
    }
    setTimeout(() => {
      setChats(c => ({ ...c, [active.id]: [...(c[active.id] || []), { me: false, text: reply }] }));
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
      {softKeyboard ? (
        <>
          <div className="glass mx-3 mb-1 flex shrink-0 items-center rounded-lg px-3 py-1.5">
            <span className="mono flex-1 truncate text-[11px] text-white/85">{cur}<span className="animate-blink">▌</span></span>
          </div>
          <SoftKeyboard
            onKey={(ch) => setCur(s => s + ch)}
            onBack={() => setCur(s => s.slice(0, -1))}
            onEnter={send}
          />
        </>
      ) : (
        <div className="glass mx-3 mb-3 flex shrink-0 items-center gap-2 rounded-xl px-3 py-2">
          <input
            value={cur}
            onChange={(e) => setCur(e.target.value)}
            onKeyDown={(e) => { if (e.key === 'Enter') send(); }}
            placeholder="сообщение агенту…"
            className="flex-1 bg-transparent text-[12px] text-white/85 outline-none"
          />
          <button onClick={send} className="text-[12px] text-accent">отправить</button>
        </div>
      )}
    </div>
  );
};

/* ---------- чат: ассистент KengaOS ---------- */

const KNOW = [
  [/тем[аыу]|оформлен|цвет/i, 'Темы переключаются в Настройках: Полярная, Синяя волна, Зелёная волна. На мобилке — в шторке.'],
  [/пророк|predict|foresee|surprise/i, 'Пророк — паттерновая память ядра: learn / predict / foresee / surprise. В ядре уже работает kf_prophet.c (v1).'],
  [/кенг[ау]|язык|kenga/i, 'Kenga — системный язык: i64/f64/str/list/Tensor/Memory, emit-c --freestanding. Примеры — в Файлах: /home/user/hello.kenga'],
  [/ядр|kernel|arch/i, 'Ядро: kmain.kenga → emit-c → нейтральный C → x86_64 и aarch64. Автотесты в QEMU на каждый коммит (CI).'],
  [/телефон|poco|mtk|мобил/i, 'Трек телефона: оболочка (сейчас) → mtkclient → fastboot boot без сноса. Цель — POCO M4 Pro (Helio G96).'],
  [/браузер|internet|сайт/i, 'Браузер открывает внутренние страницы kenga:// и внешние сайты. Свой движок отрисовки — дорожная карта.'],
  [/привет|здравств|хай/i, 'Привет! Я ассистент KengaOS. Спроси про темы, пророков, язык Кенга, ядро или телефон.'],
];

export const ChatApp = ({ softKeyboard = false }) => {
  const [msgs, setMsgs] = useState([
    { me: false, text: 'Ассистент KengaOS на связи. Спроси про: темы · пророки · язык · ядро · телефон.' },
  ]);
  const [cur, setCur] = useState('');
  const log = useRef(null);
  useEffect(() => { if (log.current) log.current.scrollTop = log.current.scrollHeight; }, [msgs]);

  const send = () => {
    const t = cur.trim();
    if (!t) return;
    setCur('');
    const hit = KNOW.find(([re]) => re.test(t));
    const reply = hit ? hit[1] : 'Записал. Пока отвечаю по темам: темы · пророки · язык · ядро · телефон.';
    setMsgs(m => [...m, { me: true, text: t }]);
    setTimeout(() => setMsgs(m => [...m, { me: false, text: reply }]), 450);
  };

  return (
    <div className="flex h-full flex-col">
      <div ref={log} className="flex-1 space-y-2 overflow-auto px-3 py-3">
        {msgs.map((m, i) => (
          <div key={i} className={`flex ${m.me ? 'justify-end' : 'justify-start'}`}>
            <div className={`max-w-[80%] rounded-2xl px-3.5 py-2 text-[12px] leading-snug ${m.me ? 'bg-accent/25 text-white' : 'glass text-white/80'}`}>
              {m.text}
            </div>
          </div>
        ))}
      </div>
      {softKeyboard ? (
        <>
          <div className="glass mx-3 mb-1 flex shrink-0 items-center rounded-lg px-3 py-1.5">
            <span className="mono flex-1 truncate text-[11px] text-white/85">{cur}<span className="animate-blink">▌</span></span>
            <button onClick={send} className="ml-2 text-[11px] text-accent">отправить</button>
          </div>
          <SoftKeyboard
            onKey={(ch) => setCur(s => s + ch)}
            onBack={() => setCur(s => s.slice(0, -1))}
            onEnter={send}
          />
        </>
      ) : (
        <div className="glass mx-3 mb-3 flex shrink-0 items-center gap-2 rounded-xl px-3 py-2">
          <input
            value={cur}
            onChange={(e) => setCur(e.target.value)}
            onKeyDown={(e) => { if (e.key === 'Enter') send(); }}
            placeholder="спроси про KengaOS…"
            className="flex-1 bg-transparent text-[12px] text-white/85 outline-none"
          />
          <button onClick={send} className="text-[12px] text-accent">отправить</button>
        </div>
      )}
    </div>
  );
};
