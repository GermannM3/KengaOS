// Сборка мобильной оболочки для APK: один entry (mobile.html), без
// code-splitting — чтобы чанки инлайнились в один файл для file:// WebView.
import { defineConfig } from 'vite';

export default defineConfig({
  base: './',
  build: {
    outDir: 'dist-apk',
    modulePreload: false,
    rollupOptions: {
      input: { mobile: 'mobile.html' },
      output: { inlineDynamicImports: true },
    },
  },
});
