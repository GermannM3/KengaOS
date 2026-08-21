import React from 'react';

const Window = ({ id, title, icon, tag, children, onClose, isFocused, onFocus, position, size }) => (
  <div
    className={`win absolute min-w-[300px] rounded-[14px] overflow-hidden flex flex-col shadow-[0_24px_60px_rgba(0,0,0,0.55),0_0_0_1px_rgba(255,255,255,0.08)] animate-pop-in
      ${isFocused ? 'shadow-[0_30px_80px_rgba(0,0,0,0.65),0_0_0_1.5px_color-mix(in_srgb,var(--accent)_55%,transparent),0_0_40px_color-mix(in_srgb,var(--accent)_18%,transparent)]' : ''}`}
    style={{ left: position.x, top: position.y, width: size.w, height: size.h, zIndex: isFocused ? 120 : 110 }}
    onClick={onFocus}
  >
    <div className="win-bar h-[40px] flex items-center gap-[10px] px-[12px] bg-white/[0.04] border-b border-white/[0.06] cursor-grab active:cursor-grabbing flex-none">
      <div className="tbtns flex gap-[7px]">
        <button className="w-3 h-3 rounded-full border-none cursor-pointer opacity-90 p-0 bg-[#ff5f57] hover:brightness-120" onClick={(e) => { e.stopPropagation(); onClose(id); }} />
        <button className="w-3 h-3 rounded-full border-none cursor-pointer opacity-90 p-0 bg-[#febc2e] hover:brightness-120" />
        <button className="w-3 h-3 rounded-full border-none cursor-pointer opacity-90 p-0 bg-[#28c840] hover:brightness-120" />
      </div>
      <div className="win-title flex items-center gap-2 text-[12px] font-semibold text-[#dfe6ff] tracking-[0.2px]">
        <svg viewBox="0 0 24 24" className="w-[15px] h-[15px] text-accent2">{icon}</svg>
        {title}
      </div>
      {tag && (
        <span className="win-tag ml-auto font-mono text-[9px] text-white/[0.35] border border-white/[0.1] px-[7px] py-[2px] rounded-[6px]">
          {tag}
        </span>
      )}
    </div>
    <div className="win-body flex-1 overflow-hidden relative bg-[rgba(8,11,20,0.35)]">
      {children}
    </div>
  </div>
);

export default Window;
