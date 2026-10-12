// OnAir 0.2.4: what changed since the 0.2.0 video, cut to a rock track (the song file is never in the repository; see footage/README.md).
// Everything is timed to the song's measured beat grid (beatmap024.js): BM.beats, BM.down (downbeats), BM.band (the band comes in),
// BM.stop / BM.slam (the full stop and the slam back), BM.final (the last hit). Mode groups and members come from the app's own mode list
// (groups024.js, generated from app/toolbar.cpp on main). Footage: the unmodified main build, each mode on its built-in test signal.
const RED='#ff5a46', HOT='#ffd2c8', V4='0.2.4';
const DB=BM.down, BEATS=BM.beats, BAND=BM.band, STOP=BM.stop, SLAM=BM.slam, FIN=BM.final, LEN4=BM.final+5.6;   // the last card holds after the ring-out
const logo4=new Image(); logo4.src='assets/logo_024.png';
const c4=(n,t)=>'file:///tmp/clips4/'+n+'/f_'+pad(clamp(Math.floor(t*30),0,(window.C4N[n]||270)-1)+1,5)+'.jpg';
function prevIn(arr,t){ let lo=0,hi=arr.length-1,r=-1; while(lo<=hi){ const m=(lo+hi)>>1; if(arr[m]<=t){ r=m; lo=m+1; } else hi=m-1; } return r; }
const beatK=t=>{ const i=prevIn(BEATS,t); return i<0?0:Math.exp(-(t-BEATS[i])*9); };
const downK=t=>{ const i=prevIn(DB,t); return i<0?0:Math.exp(-(t-DB[i])*5); };
const hitK=(t,h,r=4)=>t<h?0:Math.exp(-(t-h)*r);
const D=i=>DB[i];
// a footage frame of clip n at time t, drawn into the large texture with an optional crop camera
const CROP={cx:1112,cy:575,z:1.19};   // the mode's own area, without the sidebar
const CROPS={specan:{cx:1112,cy:520,z:1.15},search:{cx:370,cy:330,z:2.6},second:{cx:1000,cy:540,z:1.0},light:{cx:960,cy:540,z:1.0},map:{cx:1000,cy:560,z:1.1}};
function tex4(buf,n,t,cm){ return tex(buf,x=>foot(x,c4(n,t),cm||CROPS[n]||(n.startsWith('lang_')?{cx:960,cy:540,z:1}:CROP))); }
function shakeOf(t,amt){ const k=downK(t)*amt; return [(hash(Math.floor(t*60)*1.3)-.5)*k,(hash(Math.floor(t*60)*2.1)-.5)*k]; }
function flash(c,k,col='255,255,255'){ if(k<=.01) return; c.save(); c.globalCompositeOperation='lighter'; c.fillStyle=`rgba(${col},${Math.min(1,k)})`; c.fillRect(0,0,W,H); c.restore(); }
function words(c,list,x,y,size,o={}){ c.save(); c.font=`${o.w||800} ${size}px Inter`; let xx=x; if(o.al==='center'){ const tot=list.reduce((a,p)=>a+c.measureText(p[0]).width,0); xx=x-tot/2; }
  list.forEach(p=>{ txt(c,p[0],xx,y,{size,w:o.w||800,col:p[1]===1?RED:p[1]===2?PAL.accent:(o.col||'#fff'),ls:-size*.03}); xx+=c.measureText(p[0]).width; }); c.restore(); }
function kicker(c,s,x,y,a=1,col=RED,al='left'){ if(a<=0) return; txt(c,s,x,y,{size:26,w:800,col,ls:7,a,al}); }

// ---------------------------------------------------------------- 1. cold open: the riff alone. Inside the new logo's four screens, the releases roll by
const REL=[['0.2.1','15 new modes.','Light mode. Radio settings.',3],['0.2.2','Pagers, packet radio, FreeDV.','Much tougher on real-world signals.',6],['0.2.3','DVB-C. Cell towers.','A spectrum analyser.',9]];
function sIntro(c,t){
  c.fillStyle='#030405'; c.fillRect(0,0,W,H);
  // the logo, very close on its screens, pulling back over the whole riff; each riff beat lifts it a little
  const k=eio3(seg(t,0,BAND)), sc=lerp(7.5,1.25,Math.pow(k,1.6)), fx=lerp(380,512,k), fy=lerp(450,520,k);
  const s_=sc*1080/1024*.9, bk=beatK(t);
  c.save(); c.globalAlpha=ease(t,.4,2.5)*(.55+.15*bk+.3*seg(t,BAND-3.6,BAND)); c.translate(W/2,H/2); c.scale(s_,s_); c.translate(-fx,-fy); c.filter=`blur(${lerp(6,0,seg(t,0,BAND-4)).toFixed(1)}px)`; c.drawImage(logo4,0,0); c.restore();
  c.save(); const g=c.createRadialGradient(W/2,H/2,H*.2,W/2,H/2,H*.95); g.addColorStop(0,'rgba(0,0,0,.35)'); g.addColorStop(1,'rgba(0,0,0,.85)'); c.fillStyle=g; c.fillRect(0,0,W,H); c.restore();
  // words: on the downbeats of the riff
  kicker(c,'SINCE THE LAST VIDEO',W/2,330,ease(t,D(1),.6)*(1-ease(t,D(3)-.2,.3)),RED,'center');
  slam(c,[['OnAir ',0],['0.2.0',2]],W/2,520,t,D(1)+.05,{size:150,t1:D(3)-.25});
  REL.forEach(([v,l1,l2,bi],j)=>{ const t0=D(bi), t1=D(bi+3)-.25;
    kicker(c,'RELEASED',W/2,330,ease(t,t0,.4)*(1-seg(t,t1,t1+.2)),RED,'center');
    slam(c,v,W/2,560,t,t0,{size:250,t1,glow:true});
    slam(c,l1,W/2,690,t,t0+.88,{size:66,w:700,t1}); slam(c,l2,W/2,770,t,t0+1.32,{size:44,w:500,col:'#c8d2dc',t1}); });
  // 309 changes: a counter that runs over two bars
  if(t>=D(12)-.1&&t<D(14)){ const n=Math.round(309*eio3(seg(t,D(12),D(13)+.6))); slam(c,String(n),W/2,600,t,D(12),{size:330,t1:D(14)-.25,from:1.2}); slam(c,'changes later.',W/2,720,t,D(13),{size:70,w:700,t1:D(14)-.25}); }
  // the build: 0.2.4 strobes in on every beat, the frame shakes harder
  if(t>=D(14)){ const bi=prevIn(BEATS,t), on=beatK(t), p=seg(t,D(14),BAND); const [sx,sy]=[(hash(t*91)-.5)*24*p,(hash(t*57)-.5)*16*p];
    c.save(); c.translate(sx,sy); c.globalAlpha=.35+.65*on; c.shadowColor='rgba(255,90,70,.9)'; c.shadowBlur=60*on; txt(c,V4,W/2,610,{size:lerp(200,330,p),w:900,al:'center',col:on>.5?'#fff':RED,ls:-12}); c.restore();
    kicker(c,'AND NOW',W/2,330,ease(t,D(14),.3),RED,'center'); flash(c,.25*on*p); }
}
// ---------------------------------------------------------------- 2. the band comes in: the new logo and the name
function sTitle(c,t){
  cine(c,t,{glow:.25,gy:H*.45}); const lt=t-BAND, k=eio3(clamp(lt/.3)), [sx,sy]=shakeOf(t,18);
  c.save(); c.translate(sx,sy);
  const s=lerp(1.6,1,k)*(1+.015*beatK(t)); c.save(); c.translate(W/2,400); c.scale(s,s); c.shadowColor='rgba(255,90,70,.55)'; c.shadowBlur=90*hitK(t,BAND,2)+30; c.drawImage(logo4,-210,-210,420,420); c.restore();
  slam(c,[['OnAir ',0],[V4,1]],W/2,800,t,BAND+.05,{size:150,from:1.6});
  kicker(c,'NEW LOGO. NEW EVERYTHING.',W/2,890,ease(t,D(17),.4),'#c8d2dc','center');
  c.restore(); flash(c,hitK(t,BAND,5)*.9);
}
// ---------------------------------------------------------------- 3. 14 -> 77 modes
function sCount(c,t){
  cine(c,t,{glow:.2}); const [sx,sy]=shakeOf(t,12); c.save(); c.translate(sx,sy);
  const run=seg(t,D(19),D(21)), n=t<D(19)?14:t<D(21)?14+Math.floor(63*Math.pow(run,1.4)):77, big=t>=D(21);
  kicker(c,big?'NOW':'IN 0.2.0',W/2,250,big?1:ease(t,D(18),.4)*(1-seg(t,D(19)-.1,D(19)+.1)),big?RED:'#8d97a1','center');
  c.save(); c.translate(W/2,700); const s_=big?1+.25*hitK(t,D(21),5):1+.05*beatK(t); c.scale(s_,s_); if(big){ c.shadowColor='rgba(255,90,70,.8)'; c.shadowBlur=90; }
  txt(c,String(n),0,0,{size:520,w:900,al:'center',col:big?'#fff':'#d4dbe2',ls:-24}); c.restore();
  slam(c,big?'modes, in fourteen groups.':'modes',W/2,850,t,big?D(21)+.44:D(18)+.2,{size:big?70:64,w:700,t1:big?null:D(21)-.1});
  c.restore(); flash(c,hitK(t,D(21),6)*.6,'255,120,100');
}
// ---------------------------------------------------------------- 4. the fourteen groups: one bar each, the members type in on the eighth notes
const GCLIP=['mac','hdr','acars','wefax','meteor_pic','tetra','cell','pager','ble','astro','sstv','packet','hfdl','specan'];
function sGroup(c,t){
  const g=clamp(prevIn(DB,t)-22,0,13), G=GROUPS[g], t0=D(22+g), lt=t-t0, bar=D(23+g)-t0;
  cine(c,t,{glow:.18}); c.save(); const gg=c.createRadialGradient(W*.72,H*.5,0,W*.72,H*.5,W*.6); gg.addColorStop(0,G.col.replace('rgb','rgba').replace(')',',.22)')); gg.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=gg; c.fillRect(0,0,W,H); c.restore();
  const [sx,sy]=shakeOf(t,10); c.save(); c.translate(sx,sy);
  const inK=eio3(seg(lt,-.05,.22)), p=seg(lt,0,bar);
  screen3(c,tex4(TEXL[0],GCLIP[g],4+lt),{pos:[lerp(1500,430,inK),-10,lerp(900,120,inK)+lerp(0,-20,p)],w:1240,ry:lerp(-1,-.3,inK)+.06*p,rx:.03,glow:.6,sheen:p,nx:20,ny:12});
  txt(c,String(g+1).padStart(2,'0')+' / 14',110,250,{size:28,w:700,mono:true,col:G.col});
  c.save(); c.fillStyle=G.col; c.fillRect(110,272,lerp(0,120,inK),5); c.restore();
  const nm=G.name, lines=nm.length>14&&nm.includes(' ')?[nm.slice(0,nm.lastIndexOf(' ',Math.ceil(nm.length/2)+3)),nm.slice(nm.lastIndexOf(' ',Math.ceil(nm.length/2)+3)+1)]:[nm];
  lines.forEach((l,j)=>slam(c,l,110,370+j*100,t,t0+.02+j*.06,{size:lines.length>1?92:110,al:'left',from:1.3,d:.16}));
  const y0=370+lines.length*100+10, cols=G.members.length>5?2:1;
  G.members.forEach((m,j)=>{ const ta=t0+.18+j*(bar*.7/G.members.length); const k=seg(t,ta,ta+.12); if(k<=0) return; const isNew=G.new.includes(m);
    const x=110+(j%cols)*330, y=y0+Math.floor(j/cols)*50; c.save(); c.globalAlpha*=k; c.translate((1-k)*-20,0);
    txt(c,m,x,y,{size:34,w:isNew?700:500,col:isNew?'#fff':'#8d97a1'}); if(isNew){ c.font='700 34px Inter'; const w_=c.measureText(m).width; txt(c,'NEW',x+w_+12,y-2,{size:17,w:800,col:RED,ls:2}); } c.restore(); });
  c.restore(); flash(c,hitK(t,t0,9)*.22);
}
// ---------------------------------------------------------------- 5. everything else that changed: two bars each
const FEAT=[
  ['search',[['Fourteen groups.',0],['One search box.',1]],'Fold the groups you don’t use; type to find any mode.'],
  ['second',[['Two modes',0],['at once.',1]],'Run a second mode beside the first.'],
  ['light',[['Light mode.',0]],'Next to the dark palettes, with layouts for small windows.'],
  ['lang',[['Five more',0],['languages.',1]],'German, Spanish, French, Lithuanian and Chinese.'],
  ['sstv',[['Save every',0],['picture.',1]],'SSTV pictures and weather faxes, each with its own save button.'],
  ['specan',[['1 to 6000 MHz,',0],['in a sweep.',1]],'A spectrum analyser and a spectrum explorer.'],
  [null,[['More radios.',0],['Near and far.',1]],'SDRplay, Airspy HF+, PlutoSDR over the network, rtl_tcp, SpyServer, SoapyRemote.','radios'],
  [null,[['Finds the signal',0],['anywhere in the band.',1]],'Off-centre channels, mirrored spectra, clock errors and bad samples: handled.','robust'],
  [null,[['ATSC 3.0',0],['on a real broadcast.',1]],'Bootstrap, L1 and time-interleaved PLPs from a real transmission.','atsc3'],
  ['ais',[['Record IQ.',0],['Feed it out.',1]],'IQ recording, and live feeds for ADS-B, AIS, APRS, ACARS and VDL2.']];
const LANGS=['lang_de','lang_es','lang_fr','lang_lt','lang_zh'];
function sFeat(c,t){
  const i=clamp(Math.floor((prevIn(DB,t)-36)/2),0,9), t0=D(36+2*i), t1=D(38+2*i), lt=t-t0, p=seg(t,t0,t1), [clip,head,sub,kind]=FEAT[i];
  cine(c,t,{glow:.16,gy:H*.5}); const [sx,sy]=shakeOf(t,9); c.save(); c.translate(sx,sy);
  const inK=eio3(seg(lt,-.05,.25));
  if(clip){ const n=clip==='lang'?LANGS[clamp(Math.floor(lt/(t1-t0)*5),0,4)]:clip; screen3(c,tex4(TEXL[0],n,clip==='lang'?2:clip==='search'?2.2+lt*.55:3+lt),{pos:[lerp(1500,400,inK),20,lerp(900,100,inK)+lerp(0,-20,p)],w:1300,ry:lerp(-.95,-.3,inK)+.05*p,rx:.03,glow:.55,sheen:p,nx:20,ny:12}); }
  else if(kind==='radios'){ const R=['SDRplay','RSPduo tuner 2','Airspy HF+','PlutoSDR by IP','rtl_tcp','SpyServer','SoapyRemote','USRP']; R.forEach((r,j)=>{ const k=seg(t,t0+.3+j*.22,t0+.5+j*.22); if(k<=0) return; c.save(); c.globalAlpha*=k; const x=1050+(j%2)*400, y=330+Math.floor(j/2)*130; rr(c,x,y,370,96,14); c.fillStyle='#0d141b'; c.fill(); c.strokeStyle=j>=3&&j<=6?RED:'rgba(255,255,255,.18)'; c.lineWidth=2; c.stroke(); txt(c,r,x+24,y+60,{size:32,w:700}); if(j>=3&&j<=6) txt(c,'NETWORK',x+350,y+30,{size:15,w:800,col:RED,al:'right',ls:2}); c.restore(); }); }
  else if(kind==='robust'){ const off=lerp(.65,.42,eio3(seg(lt,.4,2.2))), mir=lt>1.6&&lt<2.2; c.save(); c.globalAlpha*=inK; spectrum(c,1000,300,820,420,t,[{u:off,w:.16,k:1}],{n:300,lw:2.4}); const fx=1000+off*820; c.strokeStyle=RED; c.lineWidth=3; c.strokeRect(fx-.16*820,300,.32*820,420); txt(c,mir?'mirrored: fixed':'found',fx,290,{size:24,w:800,col:RED,al:'center',ls:2}); c.restore(); }
  else if(kind==='atsc3'){ c.save(); c.globalAlpha*=inK; ['Bootstrap','L1 basic + detail','Time interleaving','LDPC + BCH','ROUTE / DASH','Picture'].forEach((s_,j)=>{ const k=seg(t,t0+.25+j*.3,t0+.45+j*.3); const y=280+j*105; rr(c,1100,y,620,80,12); c.fillStyle=k>0?'#10202c':'#0b0e12'; c.fill(); c.strokeStyle=k>0?PAL.accent:'rgba(255,255,255,.1)'; c.lineWidth=2; c.stroke(); txt(c,s_,1130,y+52,{size:32,w:700,col:k>0?'#fff':'#5d6670'}); if(k>0) check(c,1680,y+40,16,PAL.ok); }); c.restore(); }
  c.save(); const gs=c.createLinearGradient(0,0,W*.6,0); gs.addColorStop(0,'rgba(3,5,8,.8)'); gs.addColorStop(1,'rgba(3,5,8,0)'); c.fillStyle=gs; c.fillRect(0,0,W,H); c.restore();
  kicker(c,'WHAT’S NEW',110,330,ease(lt,0,.3),RED);
  head.forEach((h,j)=>slam(c,[[h[0],h[1]]],110,460+j*118,t,t0+.05+j*.2,{size:head.length>1&&h[0].length>14?92:108,al:'left',from:1.3,d:.18,t1:i<9?t1-.2:null}));
  const sy_=460+head.length*118+10; c.save(); c.globalAlpha*=seg(lt,.6,.9)*(i<9?1-seg(t,t1-.2,t1):1); c.font='400 32px Inter'; wrapText(c,sub,112,sy_,720,44,'#c8d2dc'); c.restore();
  c.restore(); flash(c,hitK(t,t0,8)*.25);
}
// ---------------------------------------------------------------- 6. into the stop: every mode name streams past, faster and faster; the stop; the slam
const ALLM=GROUPS.flatMap(g=>g.members.map(m=>[m,g.col]));
function nameRows(c,t,speed,a){ for(let r=0;r<9;r++){ const y=150+r*105, dir=r%2?1:-1; c.save(); c.globalAlpha*=a*(.35+.65*hash(r*3.7)); c.font='800 64px Inter'; let x=((t*speed*dir*(1+r*.07))%3600+3600)%3600-3600;
    for(let k=0;x<W+200;k++){ const [m,col]=ALLM[(r*11+k)%ALLM.length]; txt(c,m,x,y,{size:64,w:800,col:k%5===0?col:'#2a323b'}); x+=c.measureText(m).width+70; } c.restore(); } }
function sRun(c,t){
  c.fillStyle='#030405'; c.fillRect(0,0,W,H); const p=seg(t,D(56),STOP), sp=lerp(300,2400,p*p);
  nameRows(c,t,sp,1); const [sx,sy]=shakeOf(t,lerp(8,26,p)); c.save(); c.translate(sx,sy);
  c.save(); c.fillStyle='rgba(3,5,8,.55)'; c.fillRect(0,H/2-150,W,260); c.restore();
  slam(c,'Seventy-seven modes.',W/2,H/2+40,t,D(56)+.05,{size:130,t1:D(58)-.2});
  slam(c,[['All of them',0],[' in one app.',1]],W/2,H/2+40,t,D(58),{size:130,t1:STOP-.06});
  c.restore(); flash(c,beatK(t)*.18*p);
}
function sStop(c,t){ c.fillStyle='#000'; c.fillRect(0,0,W,H); const k=seg(t,STOP+.15,STOP+.5); txt(c,'Ready?',W/2,H/2+20,{size:40,w:500,al:'center',col:'#8d97a1',a:k*(1-seg(t,SLAM-.08,SLAM)),ls:4}); }
function sWall(c,t){
  cine(c,t,{glow:.3}); const lt=t-SLAM, k=eio3(seg(lt,0,D(68)-SLAM)), [sx,sy]=shakeOf(t,22); c.save(); c.translate(sx,sy);
  // 77 tiles on a plane in depth: names, coloured by group, with live footage in some of them; the camera flies over them
  const cols=11, tw=300, th=170, gx=26, gy=26; const ry=lerp(-.55,.35,k), rx=lerp(.5,.18,k), off=[lerp(-300,300,k),lerp(60,0,k),lerp(2600,900,eio3(seg(lt,0,1.2)))+lerp(0,-300,k)];
  const items=[]; ALLM.concat([['DVB-T2',GROUPS[0].col]]).forEach(([m,col],i)=>{ const q=rot3([((i%cols)-(cols-1)/2)*(tw+gx),(Math.floor(i/cols)-3)*(th+gy),0],rx,ry); items.push({m,col,i,pos:[q[0]+off[0],q[1]+off[1],q[2]+off[2]]}); });
  items.sort((a,b)=>b.pos[2]-a.pos[2]);
  items.forEach(it=>{ const P=(u,v)=>{ const q=rot3([((it.i%cols)-(cols-1)/2)*(tw+gx)+(u-.5)*tw,(Math.floor(it.i/cols)-3)*(th+gy)+(v-.5)*th,0],rx,ry); return proj([q[0]+off[0],q[1]+off[1],q[2]+off[2]]); };
    const cs=[P(0,0),P(1,0),P(1,1),P(0,1)]; if(cs.some(p=>p[2]<=0)) return; const fly=clamp((lt*3.2-hash(it.i*1.7)*1.2)); if(fly<=0) return;
    c.save(); c.globalAlpha*=clamp(fly); c.beginPath(); cs.forEach((p,j)=>j?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.closePath(); c.fillStyle='#0d1219'; c.fill(); c.strokeStyle=it.col; c.lineWidth=2; c.stroke();
    const cx=(cs[0][0]+cs[2][0])/2, cy=(cs[0][1]+cs[2][1])/2, s=cs[0][2]; txt(c,it.m,cx,cy+11*s,{size:Math.max(8,30*s),w:800,al:'center'}); c.restore(); });
  c.save(); c.fillStyle='rgba(3,5,8,.6)'; c.fillRect(0,H/2-140,W,250); c.restore();
  slam(c,[['ONE ',0],['APP.',1]],W/2,H/2+60,t,SLAM,{size:230,from:2.2,glow:true});
  c.restore(); flash(c,hitK(t,SLAM,3)*1.0,'255,235,225');
}
// ---------------------------------------------------------------- 7. finale: on every beat a different mode, then the last hit
const MONT4=['mac','meteor_pic','astro','sstv','hdr','ais','second','wefax','light','acars','sonde','dvbc','ft8','specan','pager'];
const MONTN={mac:'MAC TV',hdr:'HD Radio',acars:'ACARS',wefax:'Weather fax',meteor_pic:'Meteor-M LRPT',tetra:'TETRA',cell:'Cell towers',pager:'Pagers',ble:'Bluetooth LE',astro:'Radio astronomy',sstv:'SSTV',packet:'APRS',hfdl:'HFDL',specan:'Spectrum analyser',dvbc:'DVB-C',ais:'AIS',sonde:'Radiosonde',wifi:'Wi-Fi networks',ism:'ISM sensors',cubesat:'CubeSat telemetry',rid:'Drone Remote ID',nr:'5G NR cells',time:'Time signals',ft8:'FT8',map:'Map window',light:'Light mode',second:'Two modes at once'};
function sFinale(c,t){
  const lt=t-BM.joinBC;
  if(t<D(70)){ cine(c,t,{glow:.2}); const [sx,sy]=shakeOf(t,12); c.save(); c.translate(sx,sy);
    slam(c,[['Free.',0],[' Open source.',1]],W/2,470,t,D(68)+.45,{size:140});
    slam(c,'macOS · Windows · Linux (x86-64 and arm64)',W/2,600,t,D(69),{size:56,w:600,col:'#c8d2dc'}); c.restore(); flash(c,hitK(t,D(68)+.45,7)*.3); return; }
  if(t<FIN){ const bi=prevIn(BEATS,t), b0=prevIn(BEATS,D(70)), n=MONT4[(bi-b0+MONT4.length*10)%MONT4.length], ltb=t-BEATS[bi];
    const cm=CROPS[n]||CROP; foot(c,c4(n,5+ltb),{...cm,z:cm.z*(1.05+.08*seg(ltb,0,.45))});
    c.save(); c.fillStyle='rgba(3,5,8,.45)'; c.fillRect(0,0,W,H); c.restore(); scrim(c,1);
    slam(c,MONTN[n],110,H-110,t,BEATS[bi],{size:96,al:'left',from:1.2,d:.1});
    const build=seg(t,FIN-4,FIN); if(build>0){ c.save(); c.fillStyle=`rgba(0,0,0,${.6*build})`; c.fillRect(0,0,W,H); c.restore(); slam(c,'OnAir',W/2,H/2+60,t,FIN-3.4,{size:lerp(160,240,build),glow:true}); }
    flash(c,beatK(t)*.2); return; }
  // the last hit
  cine(c,t,{glow:.3,gy:H*.4}); const k=hitK(t,FIN,1.2), s=lerp(1,1.05,seg(t,FIN,LEN4));
  c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
  c.save(); c.shadowColor='rgba(255,90,70,.6)'; c.shadowBlur=80*k+20; c.drawImage(logo4,W/2-200,170,400,400); c.restore();
  slam(c,[['OnAir ',0],[V4,1]],W/2,700,t,FIN,{size:140,from:1.8});
  slam(c,'github.com/adswill/OnAir',W/2,800,t,FIN+.25,{size:60,w:700,col:'#8cc8ff',from:1.3});
  txt(c,'Free and open source. Many of the new modes are experimental: reports welcome.',W/2,880,{size:26,w:400,al:'center',col:'#8d97a1',a:seg(t,FIN+.6,FIN+1.2)});
  c.restore(); flash(c,hitK(t,FIN,5)*.75,'255,235,225');
}
// ---------------------------------------------------------------- timeline, frame loop, post
const SEQ=[[0,BAND,sIntro],[BAND,D(18),sTitle],[D(18),D(22),sCount],[D(22),D(36),sGroup],[D(36),D(56),sFeat],[D(56),STOP,sRun],[STOP,SLAM,sStop],[SLAM,BM.joinBC,sWall],[BM.joinBC,LEN4+1,sFinale]];
window.TOTAL=LEN4; window.NF=Math.round(LEN4*FPS);
function frame4(t){ const s=SEQ.find(q=>t>=q[0]&&t<q[1])||SEQ[SEQ.length-1]; ctx.save(); s[2](ctx,t); ctx.restore(); }
function post4(t,frame){
  const g=ctx.createRadialGradient(W/2,H/2,H*.55,W/2,H/2,H*1.05); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.3)'); ctx.fillStyle=g; ctx.fillRect(0,0,W,H);
  ctx.save(); ctx.globalAlpha=.03; ctx.globalCompositeOperation='overlay'; const ox=(hash(frame*1.7)*256)|0, oy=(hash(frame*2.3)*256)|0; for(let x=-ox;x<W;x+=256) for(let y=-oy;y<H;y+=256) ctx.drawImage(grain,x,y); ctx.restore();
  const o=seg(t,LEN4-1.6,LEN4); if(o>0){ ctx.fillStyle=`rgba(0,0,0,${o})`; ctx.fillRect(0,0,W,H); }
}
window.ready=Promise.all([window.ready,logo4.decode()]);
window.renderFrame=async(i)=>{ const t=i/FPS; NEED=new Set(); frame4(t); const need=NEED; NEED=null; if(need.size){ await ensure(need); frame4(t); } post4(t,i); return t; };
