import React, { useState } from 'react';
import { ICONS, APPS } from './components/Dock.jsx';
import Window from './components/Window.jsx';
import TerminalWindow from './components/TerminalWindow.jsx';
import BootScreen from './components/BootScreen.jsx';
import Wallpaper from './components/Wallpaper.jsx';
import TopBar from './components/TopBar.jsx';
import Dock from './components/Dock.jsx';
import Launcher from './components/Launcher.jsx';

const App = () => {
  const [booted, setBooted] = useState(false);
  const [launcherOpen, setLauncherOpen] = useState(false);
  const [activeApp, setActiveApp] = useState(null);
  const [windows, setWindows] = useState({});
  const [zIndex, setZIndex] = useState(110);
  const [ramPct, setRamPct] = useState(41);
  const [cpuPct, setCpuPct] = useState(12);
  const [uptime, setUptime] = useState('00:00:00');
  const [wallIdx, setWallIdx] = useState(0);

  // Boot simulation
  const handleBootComplete = () => {
    setBooted(true);
  };

  // Uptime timer
  React.useEffect(() => {
    if (!booted) return;
    const startTime = Date.now();
    const timer = setInterval(() => {
      const diff = Math.floor((Date.now() - startTime) / 1000);
      const h = String(Math.floor(diff / 3600)).padStart(2, '0');
      const m = String(Math.floor((diff % 3600) / 60)).padStart(2, '0');
      const s = String(diff % 60).padStart(2, '0');
      setUptime(`${h}:${m}:${s}`);
      
      // Simulate RAM/CPU fluctuations
      setRamPct(prev => Math.min(95, Math.max(25, prev + (Math.random() - 0.5) * 3)));
      setCpuPct(prev => Math.min(95, Math.max(5, prev + (Math.random() - 0.5) * 5)));
    }, 1000);
    return () => clearInterval(timer);
  }, [booted]);

  // Open app/window
  const openApp = (appId) => {
    if (windows[appId]) {
      focusWindow(appId);
      return;
    }
    
    const app = APPS.find(a => a.id === appId);
    if (!app) return;

    const offset = Object.keys(windows).length * 30;
    setWindows(prev => ({
      ...prev,
      [appId]: {
        id: appId,
        title: app.name,
        icon: ICONS[app.icon],
        tag: app.tag,
        position: { x: 280 + offset, y: 80 + offset },
        size: { w: appId === 'terminal' ? '640px' : appId === 'monitor' ? '620px' : '560px', h: appId === 'terminal' ? '420px' : appId === 'monitor' ? '480px' : '470px' },
        content: appId === 'terminal' ? <TerminalWindow /> : <div className="p-4 text-white/60">Содержимое окна: {app.name}</div>
      }
    }));
    setActiveApp(appId);
    setZIndex(prev => prev + 1);
  };

  const closeWindow = (appId) => {
    setWindows(prev => {
      const next = { ...prev };
      delete next[appId];
      return next;
    });
    if (activeApp === appId) {
      setActiveApp(null);
    }
  };

  const focusWindow = (appId) => {
    setActiveApp(appId);
    setZIndex(prev => prev + 1);
  };

  if (!booted) {
    return <BootScreen onComplete={handleBootComplete} />;
  }

  return (
    <div id="desktop" className="relative w-full h-full overflow-hidden">
      <Wallpaper wallIdx={wallIdx} />
      
      <TopBar ramPct={Math.round(ramPct)} cpuPct={Math.round(cpuPct)} uptime={uptime} onMenu={() => setLauncherOpen(true)} />
      
      <Dock activeApp={activeApp} onAppClick={openApp} />
      
      <div id="winLayer" className="fixed inset-0 z-[100] pointer-events-none">
        {Object.values(windows).map(win => (
          <div key={win.id} className="pointer-events-auto">
            <Window
              id={win.id}
              title={win.title}
              icon={win.icon}
              tag={win.tag}
              isFocused={activeApp === win.id}
              onFocus={() => focusWindow(win.id)}
              onClose={closeWindow}
              position={win.position}
              size={win.size}
            >
              {win.content}
            </Window>
          </div>
        ))}
      </div>
      
      <Launcher 
        isOpen={launcherOpen} 
        onClose={() => setLauncherOpen(false)} 
        onAppClick={openApp} 
      />
      
      {/* Live Log Panel */}
      <div id="liveLog" className="fixed right-[10px] bottom-[10px] w-[320px] z-[95] rounded-xl overflow-hidden glass hidden md:block">
        <div id="liveLogHead" className="flex items-center gap-2 px-3 py-2 cursor-pointer border-b border-white/[0.06]">
          <span className="statdot animate-blink" />
          <span className="mono text-[10px] tracking-[1px] text-white/[0.6]">ЖИВОЙ ЛОГ · IPC-ТРАФИК АГЕНТОВ</span>
          <span id="liveLogArrow" className="mono ml-auto text-[10px] text-white/[0.4]">▾</span>
        </div>
        <div id="liveLogList" className="font-mono text-[10px] leading-[1.8] px-3 py-2 text-white/[0.55] max-h-[132px] overflow-hidden">
          <div className="text-accent2">[14:32:15] init → ui-agent: desktop готов</div>
          <div className="text-accent">[14:32:16] shell-agent → model-agent: query XOR</div>
          <div>[14:32:17] kenga-agent → you: ответ доставлен</div>
        </div>
      </div>
    </div>
  );
};

export default App;
