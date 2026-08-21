import React from 'react';

const WALLPAPERS = [
  'https://image.qwenlm.ai/public_source/79049467-b481-47d0-b098-97fd86e25d35/1d212629b-97ba-4ef2-b7af-988e0e063ea8.png',
  'https://image.qwenlm.ai/public_source/79049467-b481-47d0-b098-97fd86e25d35/130f3f117-3618-43cb-a91c-a0fa14bed41f.png'
];

const Wallpaper = ({ wallIdx }) => (
  <>
    <img 
      id="wallpaper" 
      alt="" 
      src={WALLPAPERS[wallIdx]}
      draggable="false"
      className="fixed inset-0 w-full h-full object-cover transition-opacity duration-500 saturate-[1.05]"
    />
    <div id="wallGlow" className="fixed inset-0 pointer-events-none mix-blend-screen">
      <i className="absolute w-[55vw] h-[55vw] rounded-full blur-[90px] opacity-[0.16] bg-accent top-[-18vw] left-[-12vw] animate-drift1" />
      <i className="absolute w-[55vw] h-[55vw] rounded-full blur-[90px] opacity-[0.16] bg-accent2 bottom-[-20vw] right-[-14vw] animate-drift2" />
    </div>
    <div id="vignette" className="fixed inset-0 pointer-events-none bg-[radial-gradient(120%_90%_at_50%_40%,transparent_55%,rgba(0,0,0,0.45))]" />
  </>
);

export default Wallpaper;
