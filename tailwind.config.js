/** @type {import('tailwindcss').Config} */
module.exports = {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        accent: 'rgb(var(--accent-rgb) / <alpha-value>)',
        accent2: 'rgb(var(--accent2-rgb) / <alpha-value>)',
        bg: '#04060b',
      },
      fontFamily: {
        sans: ['Inter', 'system-ui', 'sans-serif'],
        mono: ['JetBrains Mono', 'monospace'],
        display: ['Unbounded', 'sans-serif'],
      },
      animation: {
        'fade-in': 'fadeIn 0.18s ease',
        'pop-in': 'popIn 0.22s cubic-bezier(0.2, 0.9, 0.3, 1.15)',
        'toast-in': 'toastIn 0.25s ease',
        'blink': 'blink 2.2s infinite',
        'drift1': 'drift1 26s ease-in-out infinite',
        'drift2': 'drift2 30s ease-in-out infinite',
      },
      keyframes: {
        fadeIn: {
          'from': { opacity: '0' },
          'to': { opacity: '1' },
        },
        popIn: {
          'from': { opacity: '0', transform: 'scale(0.94) translateY(8px)' },
          'to': { opacity: '1', transform: 'none' },
        },
        toastIn: {
          'from': { opacity: '0', transform: 'translateX(24px)' },
          'to': { opacity: '1', transform: 'none' },
        },
        blink: {
          '0%, 100%': { opacity: '1' },
          '50%': { opacity: '0.25' },
        },
        drift1: {
          '0%, 100%': { transform: 'translate(-6%, -4%) scale(1)' },
          '50%': { transform: 'translate(5%, 6%) scale(1.15)' },
        },
        drift2: {
          '0%, 100%': { transform: 'translate(4%, 6%) scale(1.1)' },
          '50%': { transform: 'translate(-5%, -5%) scale(1)' },
        },
      },
    },
  },
  plugins: [],
}

