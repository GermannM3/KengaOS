import React, { useState, useEffect } from 'react';
import KengaLogo from './KengaLogo';

const BOOT_LINES = [
  '[Limine] протокол 12.6.0 — запросы приняты...',
  '[OK] инициализация памяти: 94 МБ фреймов доступно',
  '[OK] GDT/IDT установлены — исключения готовы',
  '[OK] PIT настроен на 100 Гц (round-robin)',
  '[OK] heap ядра: bitmap аллокатор активен',
  '[OK] VFS смонтирован + initrd загружен',
  '[OK] агенты: init → ui-agent, model-agent, shell-agent',
  '[OK] framebuffer: 1280×800 @ 32bpp',
  '[OK] desktop environment запущен (agent-native)',
  'Добро пожаловать в KengaOS 0.5'
];

const BootScreen = ({ onComplete }) => {
  const [lines, setLines] = useState([]);
  const [progress, setProgress] = useState(0);

  useEffect(() => {
    let lineIdx = 0;
    const addLine = () => {
      if (lineIdx < BOOT_LINES.length) {
        setLines(prev => [...prev, BOOT_LINES[lineIdx]]);
        setProgress(((lineIdx + 1) / BOOT_LINES.length) * 100);
        lineIdx++;
        setTimeout(addLine, 150 + Math.random() * 200);
      } else {
        setTimeout(onComplete, 800);
      }
    };
    setTimeout(addLine, 300);
  }, [onComplete]);

  return (
    <div className="fixed inset-0 bg-bg z-[300] flex flex-col items-center justify-center gap-4">
      <KengaLogo size={86} />
      <div className="disp text-2xl tracking-[6px] bg-gradient-to-r from-white to-[#9aa7ff] bg-clip-text text-transparent">
        KENGA<span className="text-accent2">OS</span>
      </div>
      <div className="mono text-[10px] text-white/40 tracking-[2px]">
        ОПЕРАЦИОННАЯ СИСТЕМА НОВОГО ПОКОЛЕНИЯ · v0.5
      </div>
      <div className="w-[300px] h-1 bg-white/8 rounded overflow-hidden">
        <div 
          className="h-full bg-gradient-to-r from-accent to-accent2 transition-all duration-250"
          style={{ width: `${progress}%` }}
        />
      </div>
      <div className="w-[600px] max-w-[88vw] h-[150px] overflow-hidden font-mono text-[11px] leading-[1.7] text-[#7ee7d0]/90">
        {lines.map((line, i) => (
          <div key={i} className={i === lines.length - 1 ? '' : 'text-white/28'}>
            {line}
          </div>
        ))}
      </div>
    </div>
  );
};

export default BootScreen;
