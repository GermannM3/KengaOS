/* Пророк оболочки — онлайн-обучение на ходу (общий для десктопа и мобилки).

   Честно: это shell-уровень (веб). Ядро имеет свой пророк (kf_prophet.c,
   k-NN по паттернам). Здесь — та же философия на данных оболочки:
   - remember/recall — факты и пары «вопрос => ответ» (localStorage);
   - teach/answer — ассистент учится у пользователя на лету;
   - observeApp/foreseeApp — цепь Маркова: предсказание следующего
     приложения по истории открытий;
   - surprise — насколько следующее действие удивило пророка. */

const LSK = 'kenga-prophet-v1';

let M = { pairs: [], markov: {}, lastApp: null, stats: { taught: 0, obs: 0, hits: 0, surprise: 1 } };

try {
  const saved = JSON.parse(localStorage.getItem(LSK) || 'null');
  if (saved && saved.pairs) M = { ...M, ...saved };
} catch { /* первый запуск */ }

const save = () => { try { localStorage.setItem(LSK, JSON.stringify(M)); } catch { /* private mode */ } };

const vec = (s) => new Set(
  (s || '').toLowerCase().split(/[^a-zа-яё0-9]+/i).filter(w => w.length > 1)
);
const overlap = (a, b) => {
  if (!a.size || !b.size) return 0;
  let n = 0;
  for (const w of a) if (b.has(w)) n++;
  return n / Math.sqrt(a.size * b.size);
};

/* ---------- ассистент: обучение и ответы ---------- */

/* базовые знания (сид) — на них пророк опирается, пока не выучил свои */
const KNOW_SEED = [
  [/тем[аыу]|оформлен|цвет/i, 'Темы переключаются в Настройках: Полярная, Синяя волна, Зелёная волна. На мобилке — в шторке.'],
  [/пророк|predict|foresee|surprise/i, 'Пророк — паттерновая память: learn / predict / foresee / surprise. Я обучаюсь на ходу: «учи: вопрос => ответ». В ядре — kf_prophet.c.'],
  [/кенг[ау]|язык|kenga/i, 'Kenga — системный язык: i64/f64/str/list/Tensor/Memory, emit-c --freestanding. Примеры — в Файлах: /home/user/hello.kenga'],
  [/ядр|kernel|arch/i, 'Ядро: kmain.kenga → emit-c → нейтральный C → x86_64 и aarch64. Автотесты в QEMU на каждый коммит (CI).'],
  [/телефон|poco|mtk|мобил/i, 'Трек телефона: оболочка (сейчас) → mtkclient → fastboot boot без сноса. Цель — POCO M4 Pro (Helio G96).'],
  [/браузер|internet|сайт/i, 'Браузер открывает внутренние страницы kenga:// и внешние сайты. Свой движок отрисовки — в дорожной карте.'],
  [/сравнен|windows|линукс|linux|андроид|android|айос|ios/i, 'Честное сравнение с Windows/Linux/Android/iOS — docs/COMPARISON.md в репозитории. Коротко: нам до них по драйверам и экосистеме далеко; наша ставка — пророки и единый язык.'],
  [/привет|здравств|хай/i, 'Привет! Я пророк-ассистент KengaOS. Обучаюсь на ходу — научи: «учи: вопрос => ответ».'],
];

/* выучить пару «вопрос => ответ» */
export const teach = (q, a) => {
  M.pairs = M.pairs.filter(p => p.q !== q.trim());
  M.pairs.push({ q: q.trim(), a: a.trim(), v: [...vec(q)] });
  M.stats.taught++;
  save();
};

/* ответить: сначала выученное, потом сид; null — не знаю */
export const answer = (text) => {
  const q = vec(text);
  let best = null, bs = 0;
  for (const p of M.pairs) {
    const s = overlap(q, new Set(p.v));
    if (s > bs) { bs = s; best = p; }
  }
  if (best && bs >= 0.45) return { text: best.a, learned: true, score: bs };
  for (const [re, a] of KNOW_SEED) if (re.test(text)) return { text: a, learned: false, score: 1 };
  return null;
};

/* «учи: вопрос => ответ» из сырого сообщения; null если не команда обучения */
export const parseTeach = (text) => {
  const m = (text || '').match(/^(?:учи|запомни|teach)[:\s]+(.+?)\s*(?:=>|→|=)\s*(.+)$/i);
  if (!m) return null;
  teach(m[1], m[2]);
  return { q: m[1].trim(), a: m[2].trim() };
};

export const pairsCount = () => M.pairs.length;

/* ---------- предсказание действий (цепь Маркова по открытиям) ---------- */

export const APP_NAMES = {
  agents: 'Агенты', chat: 'Чат', terminal: 'Терминал', browser: 'Браузер',
  monitor: 'Монитор', files: 'Файлы', settings: 'Настройки', about: 'О системе',
  phone: 'Телефон', messages: 'Сообщения', camera: 'Камера',
};

export const observeApp = (id) => {
  const prev = M.lastApp;
  if (prev && prev !== id) {
    // score the forecast made BEFORE this transition — otherwise accuracy is a lie
    const top = foreseeApp(prev)[0];
    M.stats.surprise = surpriseApp(prev, id);
    const row = (M.markov[prev] = M.markov[prev] || {});
    row[id] = (row[id] || 0) + 1;
    M.stats.obs++;
    if (top && top.id === id) M.stats.hits++;
  }
  M.lastApp = id;
  save();
};

/* топ-3 «что откроет пользователь дальше» */
export const foreseeApp = (prev = M.lastApp) => {
  const row = M.markov[prev] || {};
  const total = Object.values(row).reduce((a, b) => a + b, 0);
  return Object.entries(row)
    .map(([id, n]) => ({ id, name: APP_NAMES[id] || id, p: total ? n / total : 0 }))
    .sort((a, b) => b.p - a.p).slice(0, 3);
};

/* удивление: 1 − p(фактический переход) — 0 = ожидаемо, 1 = шок */
export const surpriseApp = (prev, actual) => {
  const row = M.markov[prev] || {};
  const total = Object.values(row).reduce((a, b) => a + b, 0);
  if (!total) return 1;
  return 1 - (row[actual] || 0) / total;
};

export const prophetStats = () => ({
  ...M.stats,
  lastApp: M.lastApp,
  accuracy: M.stats.obs ? M.stats.hits / M.stats.obs : 0,
});
