/* Темы KengaOS: общие для десктопа и мобилки.
   Цвета — в src/index.css (:root[data-theme=...]).
   ponytail: без провайдеров и контекста — data-атрибут на <html>. */

export const THEMES = [
  { id: 'aurora', name: 'Полярная' },
  { id: 'blue', name: 'Синяя волна' },
  { id: 'green', name: 'Зелёная волна' },
];

const KEY = 'kenga-theme';

export const getTheme = () => {
  try { return localStorage.getItem(KEY) || 'aurora'; } catch { return 'aurora'; }
};

export const setTheme = (id) => {
  try { localStorage.setItem(KEY, id); } catch { /* private mode */ }
  document.documentElement.dataset.theme = id;
};

/* вызвать один раз при старте оболочки */
export const initTheme = () => { document.documentElement.dataset.theme = getTheme(); };
