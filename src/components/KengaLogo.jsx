import React from 'react';

const KengaLogo = ({ size = 48, className = '' }) => (
  <svg viewBox="0 0 48 48" width={size} height={size} className={className}>
    <defs>
      <linearGradient id="lgKenga" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0" stopColor="#8b7bff" />
        <stop offset="1" stopColor="#22d3ee" />
      </linearGradient>
    </defs>
    <path 
      d="M24 3 L41 12.5v19L24 41 7 31.5v-19z" 
      fill="none" 
      stroke="url(#lgKenga)" 
      strokeWidth="2.4"
    />
    <path 
      d="M18 15v18M18 24l12-9M18 24l12 9" 
      stroke="url(#lgKenga)" 
      strokeWidth="2.4" 
      strokeLinecap="round" 
      fill="none"
    />
  </svg>
);

export default KengaLogo;
