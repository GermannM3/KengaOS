import React, { useState } from 'react';

const TerminalWindow = () => {
  const [history, setHistory] = useState([
    { type: 'out', text: 'KengaOS 0.5 — интерактивный shell (ядро на Kenga, UTF-8 кириллица)' },
    { type: 'dim', text: "Введите 'help' для списка команд." },
    { type: 'out', text: '' }
  ]);
  const [input, setInput] = useState('');

  const handleCommand = (cmd) => {
    const newHistory = [...history, { type: 'cmd', text: cmd }];
    
    let response = '';
    const args = cmd.trim().split(/\s+/);
    const command = args[0]?.toLowerCase();

    switch (command) {
      case 'help':
        response = `help          список команд
info / ver    информация о системе
echo <текст>  вывести текст
mem           память ядра
ps / tasks    процессы (round-robin)
agents        агенты и capabilities
spawn <имя>   создать агента
log           журнал IPC
ask <вопрос>  спросить агента
model a b     MLP XOR-предсказание
ls / cat      виртуальная ФС
time / date   время и uptime
cpuinfo       CPUID
mmap          карта памяти
demo          демо
clear         очистить экран
reboot        перезагрузка
poweroff      выключение`;
        break;
      case 'ver':
      case 'info':
        response = 'KengaOS 0.5 · ядро на Kenga · x86_64 · Limine 12.6.0';
        break;
      case 'echo':
        response = args.slice(1).join(' ');
        break;
      case 'mem':
        response = 'heap: занято ~41% · свободно 55 МБ из 94 МБ\nфрейм-аллокатор: bitmap ok';
        break;
      case 'demo':
        response = '──────────────────────────────\n  K E N G A O S  ·  0.5\n  agent-native · x86_64 · kenga\n──────────────────────────────';
        break;
      case 'clear':
        setHistory([]);
        setInput('');
        return;
      case '':
        setInput('');
        return;
      default:
        response = `kenga: команда не найдена: ${command} — введите help`;
    }

    setHistory([...newHistory, { type: 'out', text: response }]);
    setInput('');
  };

  const handleKeyDown = (e) => {
    if (e.key === 'Enter') {
      handleCommand(input);
    }
  };

  return (
    <div id="termRoot" className="h-full flex flex-col bg-[rgba(3,6,12,0.6)]">
      <div id="termOut" className="term-out flex-1 font-mono text-[12px] leading-[1.65] p-3 overflow-auto">
        {history.map((item, i) => (
          <div key={i} className={`${item.type === 'cmd' ? 'text-[#9ff2e0]' : item.type === 'dim' ? 'text-white/[0.4]' : item.type === 'err' ? 'text-[#ff8f8f]' : 'text-[#e8ecf8]/[0.82]'} whitespace-pre-wrap`}>
            {item.type === 'cmd' && <span className="text-accent mr-1">❯</span>}
            {item.text}
          </div>
        ))}
      </div>
      <form 
        id="termForm" 
        className="flex items-center gap-2 px-3 pb-2"
        onSubmit={(e) => e.preventDefault()}
      >
        <span className="mono text-[12px] text-[#7ee7a3]">kenga@kengaos:~$</span>
        <input
          id="termIn"
          className="mono flex-1 min-w-0 bg-transparent border-none outline-none text-[#e8ecf8] text-[12px]"
          autoComplete="off"
          spellCheck="false"
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={handleKeyDown}
        />
      </form>
    </div>
  );
};

export default TerminalWindow;
