// The feature chapter of the 0.2.7 film: two bars each. Every line comes from the README, a doc under docs/ or a commit message of the 0.2.7 branch,
// and the caveats the project itself prints (simulator-only, receive-only, off by default) stay on screen.
const SPEED7=window.SPEED7||null; const y0Foot=S=>250+S.length*220+10;   // measured receiver load (speed027.js, written from footage/speed_027.sh): [{mode,from:{v,cores},to:{v,cores}}]
window.FEAT7=[
  {clip:'f_touchmodes',cm:{cx:960,cy:540,z:1},head:[['A touch',0],['interface.',1]],kick:'TABLETS AND TOUCH SCREENS',bullets:['48 px targets and a bottom dock','A full-screen mode picker','A keypad, swipes, long press'],col:'rgb(92,188,220)',t0:2,rate:0},
  {clip:'f_txdry',cm:{cx:960,cy:540,z:1},head:[['Transmit',0],['test signals.',1]],kick:'RECEIVER FIRST, NOW ALSO TRANSMIT',bullets:['Into a dummy load, to test receivers','Needs a radio that can transmit','Dry run: the spectrum, no RF'],col:'rgb(226,168,112)'},
  {clip:'f_images',cm:{cx:1000,cy:516,z:1.28},head:[['Weather picture',0],['studio.',1]],bullets:['Coastlines, borders, palettes','Crop, mosaic, GeoTIFF','One gallery for every picture mode'],col:'rgb(186,150,236)',t0:2,rate:.3},
  {clip:'f_passive',cm:{cx:720,cy:548,z:1.1},head:[['Passive',0],['radar.',1]],bullets:['Aircraft from the echoes of a station','Needs two coherent channels','Tested on a built-in simulator only'],col:'rgb(110,214,196)'},
  {clip:'f_df',cm:{cx:672,cy:500,z:1.45},head:[['Direction',0],['finder.',1]],bullets:['Helps you aim the antenna','Bearing from a multi-channel array','Built-in simulator to try it'],col:'rgb(226,168,112)'},
  {clip:'f_packs',cm:{cx:710,cy:430,z:1.35},head:[['Works',0],['offline.',1]],kick:'DATA PACKS',bullets:['Map tiles, satellites, airports','Nothing downloads until you click','Every file checked against its checksum'],col:'rgb(132,160,240)',t0:1,rate:.2},
  {clip:'f_rfi',cm:{cx:690,cy:430,z:1.4},head:[['Interference',0],['hunter.',1]],bullets:['Learns a band, flags what is new','Guesses what kind of source','Tested on simulated sweeps only'],col:'rgb(236,128,140)'},
  {clip:'f_passes',cm:{cx:614,cy:460,z:1.5},head:[['Satellite',0],['pass planner.',1]],bullets:['Your own TLE sources, Keplerpass','Fits the orbit from measured Doppler'],col:'rgb(186,150,236)'},
  {clip:'f_map',cm:{cx:614,cy:490,z:1.4},head:[['One map',0],['of everything.',1]],bullets:['Aircraft, ships, APRS, mesh nodes','Satellites and GNSS on it','Offline map files'],col:'rgb(92,210,148)'},
  {clip:'f_hc',head:[['Your',0],['look.',1]],kick:'THEMES',bullets:['Light and High contrast themes','Your own accent colour','Colour maps and scroll-back'],col:'rgb(236,206,92)'},
  {clip:'f_second',head:[['Two modes',0],['at once.',1]],bullets:['A second mode beside the first','Or several radios at once'],col:'rgb(92,188,220)'},
  {clip:'f_p25',head:[['Follow trunked',0],['calls.',1]],bullets:['P25 and DMR Tier III control channels','Follows the calls it announces','Receive only'],col:'rgb(150,206,104)'},
  {head:[['Clean',0],['audio.',1]],kick:'NEURAL NOISE REMOVER',bullets:['Speech in airband and the general receiver','A strength slider, off by default','Decoders always get the raw sound'],col:'rgb(92,210,148)',
    draw(c,t,lt,p,left,inK){ const x0=left?W-1100:140, w=960, y0=500, cl=eio3(seg(lt,.5,2.4));
      c.save(); c.globalAlpha*=inK; rr(c,x0-20,y0-210,w+40,420,18); c.fillStyle='#0b1117'; c.fill(); c.strokeStyle='rgba(255,255,255,.14)'; c.lineWidth=2; c.stroke();
      c.beginPath(); for(let i=0;i<=w;i+=3){ const u=i/w, env=Math.exp(-Math.pow((u-.5)*3.2,2))*(.55+.45*Math.sin(u*40+t*2))*(Math.sin(u*210+t*9)); const noise=(hash(i*7.3+Math.floor(t*30)*3.1)-.5)*2*(1-cl*.93); const v=(env*.9+noise*.5)*150; i?c.lineTo(x0+i,y0+v):c.moveTo(x0+i,y0+v); }
      c.strokeStyle=cl>.5?PAL.accent:'#9aa4ae'; c.lineWidth=2.4; c.stroke(); txt(c,cl>.5?'cleaned':'noisy',x0,y0-170,{size:30,w:800,col:cl>.5?PAL.accent:'#9aa4ae',ls:3}); c.restore(); }},
  {head:[['Radios',0],['everywhere.',1]],kick:'MORE RADIOS',col:'rgb(92,168,219)',
    draw(c,t,lt,p,left,inK){ const R=['HackRF','RTL-SDR','Airspy','SDRplay','BladeRF','LimeSDR','USRP','PlutoSDR','Fobos SDR','RX888 MkII','RFNM','Red Pitaya','Hermes Lite 2','KiwiSDR','SpyServer','SDR++ server','Sound card','Hamlib rigs'];
      R.forEach((r,j)=>{ const k=seg(lt,.12+j*.07,.3+j*.07); if(k<=0) return; const x=(left?W-1000:140)+(j%3)*320, y=250+Math.floor(j/3)*118; c.save(); c.globalAlpha*=k; c.translate(0,(1-k)*26); rr(c,x,y,296,92,14); c.fillStyle='#0d141b'; c.fill();
        const isNew=['Fobos SDR','RX888 MkII','RFNM','Red Pitaya','Hermes Lite 2','KiwiSDR','SpyServer','SDR++ server','Sound card','Hamlib rigs'].includes(r); c.strokeStyle=isNew?RED:'rgba(255,255,255,.18)'; c.lineWidth=2; c.stroke(); txt(c,r,x+22,y+57,{size:31,w:700}); if(isNew) txt(c,'NEW',x+276,y+28,{size:15,w:800,col:RED,al:'right',ls:2}); c.restore(); }); }},
  {head:[['Less CPU.',1]],kick:'MEASURED',col:'rgb(92,168,219)',
    draw(c,t,lt,p,left,inK){ const S=SPEED7||[]; if(!S.length){ return; } const bx=(left?W-1130:150)+330, per=560/4;
      S.forEach((r,g)=>{ const y0=250+g*220; txt(c,r.mode,(left?W-1130:150),y0+22,{size:44,w:700,a:ease(lt,.1+g*.4,.4)});
        [['from',r.from],['to',r.to]].forEach(([w,e],j)=>{ const y=y0+54+j*70, k=eio3(seg(lt,.3+g*.4+j*.35,1.1+g*.4+j*.35)), now=w==='to';
          txt(c,e.v,(left?W-1130:150),y+38,{size:30,w:700,mono:true,col:now?PAL.accent:'#8d97a1',a:ease(lt,.2+g*.4+j*.3,.3)});
          rr(c,bx,y+6,Math.max(8,e.cores*per*k),48,8); c.fillStyle=now?PAL.accent:'#5d6670'; c.fill(); txt(c,(e.cores*k).toFixed(1)+(e.cores*k>=.95&&e.cores*k<1.05?' core':' cores'),bx+e.cores*per*k+16,y+40,{size:30,w:700,col:now?'#fff':'#aeb8c2',a:k}); }); });
      txt(c,'Same recording, same machine, dect2cli\u2019s own load meter. ATSC and DAB: unchanged.',(left?W-1130:150),y0Foot(S),{size:21,w:400,col:'#7f8a96',a:ease(lt,1.6,.4)}); }},
  {head:[['Everywhere.',1]],kick:'ON YOUR SCREEN, YOUR WAY',col:'rgb(132,160,240)',
    draw(c,t,lt,p,left,inK){ const P=[['macOS','Apple silicon and Intel'],['Windows','10 and 11, 64-bit'],['Linux','x86-64 and arm64'],['Android','receive only'],['Docker','the GUI in a browser']];
      P.forEach(([a,b],j)=>{ const k=seg(lt,.15+j*.12,.35+j*.12); if(k<=0) return; const x=(left?W-1000:140)+(j%2)*470, y=290+Math.floor(j/2)*200; c.save(); c.globalAlpha*=k; c.translate(0,(1-k)*26); rr(c,x,y,440,160,16); c.fillStyle='#0d141b'; c.fill(); c.strokeStyle=j>=3?RED:'rgba(255,255,255,.2)'; c.lineWidth=2; c.stroke();
        txt(c,a,x+28,y+70,{size:46,w:800}); txt(c,b,x+28,y+118,{size:28,w:500,col:'#aeb8c2'}); if(j>=3) txt(c,'NEW',x+412,y+34,{size:16,w:800,col:RED,al:'right',ls:2}); c.restore(); }); }}
];
