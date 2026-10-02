// OnAir promo v2: real screen captures of the app (headless build of the real UI code) + kinetic type.
// Needs common_head.js (helpers). window.renderFrame(i) is async because clip frames are loaded on demand.
const CLIP = { tv:{n:228,fps:6}, t2:{n:150,fps:6}, dvbt:{n:60,fps:6}, echo:{n:96,fps:6}, noisy:{n:36,fps:6}, ant:{n:30,fps:6} };
const imgCache = new Map(), need = new Set(); let frameNo = 0;
const ckey = (n,i)=>n+'/'+String(i+1).padStart(4,'0');
function getImg(n,i){ const k=ckey(n,i), e=imgCache.get(k); if(e){e.use=frameNo; return e.img;} need.add(k); return null; }
async function loadImg(k){ const img=new Image(); img.src='clips/'+k+'.jpg'; try{await img.decode();}catch(e){console.log('decode fail',k);} imgCache.set(k,{img,use:frameNo}); }
// draw a (blended) frame of a clip; view = [x,y,w,h] in the app's 1500x900 logical pixels
const V_FULL=[0,0,1500,900];
function drawClip(c,name,t,dx,dy,dw,dh,view=V_FULL,alpha=1){
  const cl=CLIP[name]; const f=clamp(t*cl.fps,0,cl.n-1.001), i0=Math.floor(f), a=f-i0;
  const im0=getImg(name,i0), im1=a>0.04?getImg(name,i0+1):null; if(!im0) return;
  const sx=view[0]*2, sy=view[1]*2, sw=view[2]*2, sh=view[3]*2;
  c.save(); c.imageSmoothingEnabled=true; c.imageSmoothingQuality='high'; c.globalAlpha*=alpha;
  c.drawImage(im0,sx,sy,sw,sh,dx,dy,dw,dh);
  if(im1){ c.globalAlpha*=a; c.drawImage(im1,sx,sy,sw,sh,dx,dy,dw,dh); }
  c.restore();
}
function clipCard(c,name,t,x,y,w,h,view=V_FULL,o={}){
  const r=o.r??14; c.save();
  if(!o.noShadow){ c.shadowColor='rgba(0,0,0,.65)'; c.shadowBlur=60; c.shadowOffsetY=24; rr(c,x,y,w,h,r); c.fillStyle='#0b0c0d'; c.fill(); c.shadowColor='transparent'; }
  rr(c,x,y,w,h,r); c.clip(); c.fillStyle='#0b0c0d'; c.fillRect(x,y,w,h);
  if(o.t2!=null) drawClip(c,o.name2||name,o.t2,x,y,w,h,view,o.mix??0);
  drawClip(c,name,t,x,y,w,h,view,1-(o.mix??0)); c.restore();
  c.save(); rr(c,x+.5,y+.5,w-1,h-1,r); c.strokeStyle=o.border||'rgba(255,255,255,.16)'; c.lineWidth=1.5; c.stroke(); c.restore();
}
const mapV=(view,x,y,w,h,lx,ly)=>[x+(lx-view[0])/view[2]*w, y+(ly-view[1])/view[3]*h];
function callout(c,label,p1,p2,a,col=C.blue,size=24){
  if(a<=0) return; c.save(); c.globalAlpha*=a; const k=eo5(a);
  c.strokeStyle=col; c.lineWidth=2.5; c.beginPath(); c.moveTo(p1[0],p1[1]); c.lineTo(p1[0]+(p2[0]-p1[0])*k,p1[1]+(p2[1]-p1[1])*k); c.stroke();
  c.fillStyle=col; c.beginPath(); c.arc(p1[0],p1[1],7,0,7); c.fill(); c.globalAlpha*=.25; c.beginPath(); c.arc(p1[0],p1[1],15,0,7); c.fill(); c.globalAlpha=Math.min(1,a*1.4);
  c.font=`600 ${size}px Inter`; const tw=c.measureText(label).width+40, th=size+22; rr(c,p2[0]-tw/2,p2[1]-th/2,tw,th,th/2); c.fillStyle='rgba(6,10,14,.94)'; c.fill(); c.strokeStyle=col; c.lineWidth=2; c.stroke();
  txt(c,label,p2[0],p2[1]+size*.35,{size,w:600,al:'center'}); c.restore();
}
function head(c,l1,l2,t,t0,o={}){ const x=o.x??110, y=o.y??130, s=o.size??64;
  reveal(c,l1,x,y,t,t0,{size:s,w:500,ls:-s*.035,al:o.al||'left'}); if(l2) reveal(c,l2,x,y+s*1.08,t,t0+.3,{size:s,w:500,ls:-s*.035,col:o.col||C.blue,al:o.al||'left'}); }
function bgd(c,glow=.7){ c.fillStyle=C.bg; c.fillRect(0,0,W,H); const g=c.createRadialGradient(W*.65,H*.55,0,W*.65,H*.55,W*.7); g.addColorStop(0,`rgba(40,110,170,${.16*glow})`); g.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g; c.fillRect(0,0,W,H); }
function persp(c,src,cx,cy,sc,angDeg,alpha=1){
  const sw=src.width, sh=src.height, ang=angDeg*Math.PI/180, strip=3, f=2600; c.save(); c.globalAlpha*=alpha;
  for(let x=0;x<sw;x+=strip){ const lx=(x-sw/2)*sc, z=lx*Math.sin(ang), px=lx*Math.cos(ang), s=f/(f+z), h=sh*sc*s; c.drawImage(src,x,0,strip,sh,cx+px*s,cy-h/2,strip*sc*Math.cos(ang)*s+.6,h); } c.restore();
}
const lerpV=(a,b,k)=>a.map((v,i)=>lerp(v,b[i],k));
const flash=(c,a)=>{ if(a>0){c.save();c.fillStyle=`rgba(255,255,255,${a})`;c.fillRect(0,0,W,H);c.restore();} };
function scan(c,a=.12){ c.save(); c.fillStyle=`rgba(0,0,0,${a})`; for(let y=0;y<H;y+=4) c.fillRect(0,y+2,W,2); c.restore(); }

const T={};
// ---- hook: Tune. Lock. Watch.
T.hook=(c,t)=>{
  c.fillStyle='#000'; c.fillRect(0,0,W,H);
  const P=(t0,t1)=>t>=t0&&t<t1;
  const punch=(t0)=>{const k=eo5(seg(t,t0,t0+.55)); return 1.14-.14*k;};
  // 1: Tune. spectrum + waterfall
  if(P(0,2.0)){ const s=punch(0.0); c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
    c.fillStyle='#050607'; c.fillRect(0,0,W,H); drawClip(c,'t2',8+t*.2,0,130,W,795,V_SPEC); c.fillStyle='rgba(0,0,0,.30)'; c.fillRect(0,0,W,H); c.restore();
    // tuning readout
    const f=lerp(517.6,522.0,eo3(seg(t,.2,1.6))); txt(c,f.toFixed(3)+' MHz',W-120,170,{size:64,w:500,mono:true,al:'right',col:C.ink});
    reveal(c,'Tune.',110,900,t,.05,{size:210,w:700,ls:-9}); }
  // 2: Lock. 64-QAM constellation
  if(P(2.0,4.0)){ const tt=t-2.0, s=punch(2.0); c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
    c.fillStyle='#050607'; c.fillRect(0,0,W,H);
    drawClip(c,'dvbt',3+tt*.3,1000,68,860,944,CONST_DATA); c.restore();
    reveal(c,'Lock.',110,900,t,2.05,{size:210,w:700,ls:-9}); reveal(c,'64-QAM. Every point, sharp.',116,980,t,2.4,{size:34,w:400,col:C.dim}); }
  // 3: Watch. decoded video, full bleed
  if(P(4.0,6.2)){ const tt=t-4.0, s=punch(4.0)+.0; c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
    drawClip(c,'tv',6+tt*.9,0,0,W,H,V_VIDEO); c.fillStyle='rgba(0,0,0,.18)'; c.fillRect(0,0,W,H); c.restore(); scan(c,.14);
    const g=c.createLinearGradient(0,H*.55,0,H); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.75)'); c.fillStyle=g; c.fillRect(0,H*.55,W,H*.45);
    reveal(c,'Watch.',110,960,t,4.05,{size:210,w:700,ls:-9}); }
  // flashes on the cuts
  flash(c,.55*(1-seg(t,0,.12))); flash(c,.5*(1-seg(t,2.0,2.14))*(t>=2.0)); flash(c,.5*(1-seg(t,4.0,4.14))*(t>=4.0));
};
T.logo=(c,t)=>{
  bgd(c,seg(t,0,1));
  const lk=eoBack(seg(t,.1,.9)); c.save(); c.translate(W/2,380); c.scale(lk,lk);
  const gl=c.createRadialGradient(0,0,0,0,0,380); gl.addColorStop(0,'rgba(255,59,48,.22)'); gl.addColorStop(1,'rgba(255,59,48,0)'); c.fillStyle=gl; c.fillRect(-420,-420,840,840);
  c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=50; c.shadowOffsetY=20; c.globalAlpha=seg(t,.1,.4); c.drawImage(logo,-170,-170,340,340); c.restore();
  reveal(c,'OnAir',W/2,740,t,.9,{size:120,w:600,ls:-5,al:'center'});
  reveal(c,'Digital TV, decoded in software.',W/2,820,t,1.5,{size:46,w:400,col:C.ink,ls:-1,al:'center'});
  reveal(c,'DVB-T2 · DVB-T · ATSC — for HackRF. Open source.',W/2,885,t,2.1,{size:30,w:400,col:C.dim,al:'center'});
};
// regions of the app window (logical px), checked against real frames
const V_VIDEO=[276,200,601,338], CONST_DATA=[814,646,204,224], V_SPEC=[10,176,1135,440], V_CROW=[140,646,1215,224];
const winBuf=mk(1500,900);
T.live=(c,t)=>{
  bgd(c,.9);
  const wc=winBuf.getContext('2d'); wc.clearRect(0,0,1500,900);
  const zk=eio(seg(t,3.4,4.3))*(1-eio(seg(t,6.0,6.6)));
  const view=lerpV(V_FULL,[150,150,840,504],zk);
  const guide=eio(seg(t,6.4,7.0));
  wc.save(); rr(wc,0,0,1500,900,14); wc.clip(); wc.fillStyle='#0b0c0d'; wc.fillRect(0,0,1500,900);
  // the window itself is drawn through the view (zoom keeps the frame size)
  drawClip(wc,'tv',6+t*.95,0,0,1500,900,view,1-guide); if(guide>0) drawClip(wc,'tv',16+(t-6.4),0,0,1500,900,view,guide);
  wc.restore(); wc.save(); rr(wc,.75,.75,1498.5,898.5,14); wc.strokeStyle='rgba(255,255,255,.16)'; wc.lineWidth=1.5; wc.stroke(); wc.restore();
  const k=eo5(seg(t,.2,1.7)), ang=lerp(-40,0,k), sc=lerp(.62,.9,k), cx=lerp(1100,960,k), cy=590;
  c.save(); c.globalAlpha=k; c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=70; c.shadowOffsetY=30; persp(c,winBuf,cx,cy,sc,ang); c.restore();
  const capA=1-seg(t,6.0,6.5);
  c.save(); c.globalAlpha=capA; head(c,'Plug in a HackRF.','Watch live TV.',t,.3,{x:960,y:100,al:'center',size:58}); c.restore();
  c.save(); c.globalAlpha=eo3(seg(t,6.6,7.2)); head(c,'Programme guide. Subtitles.','Teletext.',t,6.6,{x:960,y:100,al:'center',size:58}); c.restore();
  // callouts (only at full view); the window is drawn at scale .9, centred
  const M=(lx,ly)=>[960-675+lx*.9, 590-405+ly*.9];
  const cl=(1-zk)*(1-guide);
  callout(c,'Live video and audio',M(600,400),M(580,625),eo3(seg(t,1.9,2.5))*cl,C.blue);
  callout(c,'Services found automatically',M(1320,235),M(1320,340),eo3(seg(t,2.4,3.0))*cl,C.green);
  callout(c,'Signal quality, live',M(800,178),M(1010,125),eo3(seg(t,2.9,3.5))*cl,C.amber);
};
T.auto=(c,t)=>{
  bgd(c,.9);
  head(c,'It finds the signal.','You don’t configure a thing.',t,.2,{size:58,y:140});
  const items=[['Detects the standard','DVB-T2, DVB-T or ATSC'],['Measures the bandwidth','1.7 to 8 MHz, automatically'],['Reads the signalling','FFT size, guard interval, pilots'],['Locks and decodes','then keeps tracking drift']];
  items.forEach(([a,b],i)=>{ const k=eo5(seg(t,.9+i*.7,1.6+i*.7)); if(k<=0)return; c.save(); c.globalAlpha=k; c.translate(-(1-k)*40,0);
    const y=330+i*150; rr(c,110,y-46,560,118,18); c.fillStyle='rgba(255,255,255,.035)'; c.fill(); c.strokeStyle='rgba(255,255,255,.1)'; c.lineWidth=1.2; c.stroke();
    c.fillStyle=C.green; c.beginPath(); c.arc(160,y+13,22,0,7); c.fill(); c.strokeStyle='#04140b'; c.lineWidth=5; c.lineCap='round'; c.beginPath(); c.moveTo(150,y+14); c.lineTo(158,y+22); c.lineTo(172,y+4); c.stroke();
    txt(c,a,200,y+6,{size:32,w:600,ls:-.5}); txt(c,b,200,y+44,{size:22,col:C.dim}); c.restore(); });
  const k=eo5(seg(t,.3,1.4)), w=1130, h=678, x=730, y=300+(1-k)*60; c.save(); c.globalAlpha=k;
  const view=lerpV(V_FULL,[0,40,1150,690],eio(seg(t,4.0,5.2))); clipCard(c,'t2',4+t*.7,x,y,w,h,view); c.restore();
  const vz=eio(seg(t,4.0,5.2)); const M=(lx,ly)=>mapV(view,x,y,w,h,lx,ly);
  callout(c,'Standard: auto-detected',M(250,44),[x+330,y-40],eo3(seg(t,5.0,5.6)),C.blue,22);
  callout(c,'FFT 8K · GI 1/128 · PP1 — read from the signal',M(1050,78),[x+700,y+h+46],eo3(seg(t,5.5,6.1)),C.green,22);
};
T.see=(c,t)=>{
  bgd(c,.9);
  head(c,'See what the radio sees.',null,t,.2,{size:62,y:140});
  reveal(c,'Spectrum, waterfall, constellations, channel response, FEC \u2014 all live.',110,205,t,.6,{size:28,w:400,col:C.dim,ls:-.3});
  const k=eo5(seg(t,.4,1.5));
  c.save(); c.globalAlpha=k; c.translate(0,(1-k)*40); clipCard(c,'t2',2+t*.7,110,270,1100,426,V_SPEC); c.restore();
  const k2=eo5(seg(t,.9,1.9)); c.save(); c.globalAlpha=k2; c.translate(0,(1-k2)*40); clipCard(c,'dvbt',3+t*.3,110,740,1100,203,V_CROW); c.restore();
  const kk=eo5(seg(t,1.4,2.4)); c.save(); c.globalAlpha=kk; c.translate(0,(1-kk)*40);
  const sz=262, xA=1290, xB=1290+sz+26, yy=270;
  clipCard(c,'noisy',1+(t*.6)%4,xA,yy,sz,sz*224/204,CONST_DATA); clipCard(c,'dvbt',3+(t*.4)%5,xB,yy,sz,sz*224/204,CONST_DATA);
  const ly=yy+sz*224/204;
  txt(c,'Weak signal',xA,ly+44,{size:26,w:600,col:C.amber}); txt(c,'SNR 9 dB',xA,ly+78,{size:22,mono:true,col:C.dim});
  txt(c,'Clean signal',xB,ly+44,{size:26,w:600,col:C.green}); txt(c,'SNR 27 dB',xB,ly+78,{size:22,mono:true,col:C.dim});
  c.restore();
  const m=eo3(seg(t,3.2,4.2)); c.save(); c.globalAlpha=m; txt(c,'Watch the noise turn into data.',1290,ly+150,{size:32,w:500,col:C.ink,ls:-.5}); c.restore();
};
T.fix=(c,t)=>{
  bgd(c,.9);
  head(c,'Know what’s wrong.','Know how to fix it.',t,.2,{size:62,y:140});
  const k=eo5(seg(t,.4,1.5)); const sw=eio(seg(t,5.4,6.2));
  c.save(); c.globalAlpha=k*(1-sw); clipCard(c,'echo',2+t*.7,110,330,1180,650,[0,170,1170,640]); c.restore();
  c.save(); c.globalAlpha=sw; clipCard(c,'ant',1+(t-5.4)*.4,110,330,1180,650,[0,170,1500,826]); c.restore();
  const A=eo3(seg(t,1.8,2.5))*(1-sw), B=eo3(seg(t,3.0,3.7))*(1-sw);
  callout(c,'Multipath detector',[ 110+1180*.18,330+650*.2],[1500,420],A,C.amber);
  callout(c,'Echo vs. guard interval',[110+1180*.62,330+650*.45],[1560,640],B,C.green);
  c.save(); c.globalAlpha=sw; callout(c,'Antenna direction finder',[110+1180*.5,330+650*.5],[1560,460],eo3(seg(t,6.2,6.9)),C.blue); c.restore();
  const side=eo3(seg(t,4.2,5.0))*(1-sw); c.save(); c.globalAlpha=side; txt(c,'Signal quality, gain helper,',1400,820,{size:28,w:400,col:C.dim,al:'center'}); txt(c,'multipath report — in plain words.',1400,858,{size:28,w:400,col:C.dim,al:'center'}); c.restore();
  c.save(); c.globalAlpha=eo3(seg(t,6.6,7.3)); txt(c,'Where to point the antenna? It measures every direction.',1400,820,{size:28,w:400,col:C.dim,al:'center'}); c.restore();
};
T.out=(c,t)=>{
  bgd(c,.9);
  head(c,'Record it. Stream it.','Use it anywhere.',t,.2,{size:62,y:140});
  const k=eo5(seg(t,.4,1.5)); const sw=eio(seg(t,2.8,3.6));
  c.save(); c.globalAlpha=k*(1-sw); clipCard(c,'tv',28.5+t*.5,110,330,1180,600,[0,118,1150,590]); c.restore();
  c.save(); c.globalAlpha=sw; clipCard(c,'tv',32+(t-2.8)*.5,110,330,1180,600,[0,118,1150,590]); c.restore();
  ['.ts file','UDP → VLC · ffplay · tvheadend','Live player'].forEach((s,i)=>{ const a=eo5(seg(t,1.4+i*.35,2.1+i*.35)); if(a<=0)return; c.save(); c.globalAlpha=a; c.translate((1-a)*50,0);
    rr(c,1360,350+i*120,430,92,18); c.fillStyle='rgba(255,255,255,.04)'; c.fill(); c.strokeStyle='rgba(92,192,255,.5)'; c.lineWidth=1.5; c.stroke(); txt(c,s,1388,408+i*120,{size:i===1?25:34,w:600}); c.restore(); });
};
T.outro=(c,t)=>{
  bgd(c,1);
  const lk=eoBack(seg(t,.2,1.0)); c.save(); c.translate(W/2,330); c.scale(lk,lk); c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=60; c.shadowOffsetY=24; c.globalAlpha=seg(t,.2,.6); c.drawImage(logo,-190,-190,380,380); c.restore();
  const p=.5+.5*Math.sin(t*3.2); const gg=c.createRadialGradient(W/2+105,350,0,W/2+105,350,220); gg.addColorStop(0,`rgba(255,59,48,${.18+.14*p})`); gg.addColorStop(1,'rgba(255,59,48,0)'); c.fillStyle=gg; c.globalAlpha=seg(t,.8,1.4); c.fillRect(W/2-300,150,800,400); c.globalAlpha=1;
  reveal(c,'OnAir',W/2,700,t,1.0,{size:170,w:600,ls:-7,al:'center'});
  reveal(c,'Open-source digital TV receiver for HackRF.',W/2,790,t,1.5,{size:42,w:400,col:C.dim,ls:-.8,al:'center'});
  const chips=['DVB-T2','DVB-T','ATSC','macOS','Linux'];
  const tw=chips.map(s=>s.length*14+56), tot=tw.reduce((a,b)=>a+b+16,-16); let cx=W/2-tot/2;
  chips.forEach((s,i)=>{const a=eo5(seg(t,2.0+i*.12,2.6+i*.12)); c.save();c.globalAlpha=a;c.translate(0,(1-a)*16); rr(c,cx,830,tw[i],46,23);c.strokeStyle='#2c333a';c.stroke();txt(c,s,cx+tw[i]/2,860,{size:20,al:'center',col:C.ink,w:500}); c.restore(); cx+=tw[i]+16;});
  reveal(c,'github.com/adswill/OnAir',W/2,960,t,3.2,{size:32,w:500,col:C.blue,mono:true,al:'center'});
};

// ---- timeline (scene, length, transition-in, overlap)
const SCENES=[['hook',6.2,null,0],['logo',5.4,'zoom',.7],['live',9.6,'sweep',.8],['auto',7.6,'iris',.8],['see',7.6,'push',.7],['fix',9.0,'zoom',.7],['out',6.2,'slices',.7],['outro',7.6,'iris',.9]];
let cur=0; for(const s of SCENES){ s.t0=cur===0?0:cur-s[3]; s.t1=s.t0+s[1]; cur=s.t1; }
const TOTAL=cur; window.TOTAL=TOTAL; window.SCENES=SCENES.map(s=>({name:s[0],t0:s.t0,t1:s.t1,tr:s[2],trd:s[3]}));
const bufA=mk(), bufB=mk(), ca=bufA.getContext('2d'), cb=bufB.getContext('2d');
function sceneAt(c,s,t){ c.clearRect(0,0,W,H); T[s[0]](c,t-s.t0); }
function composite(t){
  const act=SCENES.filter(s=>t>=s.t0&&t<s.t1);
  if(act.length===1){ sceneAt(ctx,act[0],t); return; }
  const A=act[0],B=act[1], p=clamp((t-B.t0)/B[3]); sceneAt(ca,A,t); sceneAt(cb,B,t);
  const e=eio(p), type=B[2]; ctx.fillStyle='#000'; ctx.fillRect(0,0,W,H);
  if(type==='iris'){ ctx.drawImage(bufA,0,0); ctx.save(); const r=e*Math.hypot(W,H)*.75, ox=W/2, oy=H*.42; ctx.beginPath(); ctx.arc(ox,oy,r,0,7); ctx.clip(); ctx.drawImage(bufB,0,0); ctx.restore();
    ctx.save(); ctx.strokeStyle=C.blue; ctx.globalAlpha=(1-e)*.9; ctx.lineWidth=3; ctx.shadowColor=C.blue; ctx.shadowBlur=30; ctx.beginPath(); ctx.arc(ox,oy,r,0,7); ctx.stroke(); ctx.restore(); }
  else if(type==='sweep'){ ctx.drawImage(bufA,0,0); const x=lerp(-300,W+300,e), sk=240; ctx.save(); ctx.beginPath(); ctx.moveTo(0,0); ctx.lineTo(x+sk,0); ctx.lineTo(x-sk,H); ctx.lineTo(0,H); ctx.closePath(); ctx.clip(); ctx.drawImage(bufB,0,0); ctx.restore();
    ctx.save(); const g=ctx.createLinearGradient(x-90,0,x+90,0); g.addColorStop(0,'rgba(92,192,255,0)'); g.addColorStop(.5,'rgba(190,230,255,.55)'); g.addColorStop(1,'rgba(92,192,255,0)'); ctx.fillStyle=g; ctx.globalAlpha=Math.sin(p*Math.PI); ctx.beginPath(); ctx.moveTo(x+sk-90,0); ctx.lineTo(x+sk+90,0); ctx.lineTo(x-sk+90,H); ctx.lineTo(x-sk-90,H); ctx.closePath(); ctx.fill(); ctx.restore(); }
  else if(type==='zoom'){ ctx.save(); ctx.globalAlpha=1-eo3(p); const s1=1+e*.35; ctx.translate(W/2,H/2); ctx.scale(s1,s1); ctx.translate(-W/2,-H/2); ctx.drawImage(bufA,0,0); ctx.restore();
    ctx.save(); ctx.globalAlpha=eo3(p); const s2=1.18-e*.18; ctx.translate(W/2,H/2); ctx.scale(s2,s2); ctx.translate(-W/2,-H/2); ctx.drawImage(bufB,0,0); ctx.restore(); }
  else if(type==='push'){ ctx.save(); ctx.translate(-e*W*.45,0); ctx.globalAlpha=1-e*.8; ctx.drawImage(bufA,0,0); ctx.restore(); ctx.save(); ctx.translate((1-e)*W,0); ctx.drawImage(bufB,0,0); ctx.restore(); }
  else if(type==='slices'){ ctx.drawImage(bufA,0,0); const n=12, sw=W/n; for(let i=0;i<n;i++){ const qq=eo3(clamp(p*1.5-(i/n)*.5)); ctx.save(); ctx.beginPath(); ctx.rect(i*sw,H*(1-qq),sw+1,H*qq); ctx.clip(); ctx.drawImage(bufB,0,0); ctx.restore(); } }
  else { ctx.drawImage(bufA,0,0); ctx.save(); ctx.globalAlpha=e; ctx.drawImage(bufB,0,0); ctx.restore(); }
}
function post(t){
  const g=ctx.createRadialGradient(W/2,H/2,H*.45,W/2,H/2,H*1.0); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.45)'); ctx.fillStyle=g; ctx.fillRect(0,0,W,H);
  const o=seg(t,TOTAL-0.9,TOTAL); if(o>0){ctx.fillStyle=`rgba(0,0,0,${o})`;ctx.fillRect(0,0,W,H)}
}
window.ready=Promise.all([document.fonts.load('500 20px Inter'),document.fonts.load('600 20px Inter'),document.fonts.load('400 20px Inter'),document.fonts.load('700 20px Inter'),document.fonts.load('500 20px "JetBrains Mono"'),logo.decode()]);
window.renderFrame=async(i)=>{ frameNo=i; const t=i/FPS;
  for(let pass=0;pass<4;pass++){ need.clear(); composite(t); post(t); if(!need.size) break; await Promise.all([...need].map(loadImg)); }
  if(imgCache.size>20){ const arr=[...imgCache.entries()].sort((a,b)=>a[1].use-b[1].use); for(let k=0;k<arr.length-16;k++) imgCache.delete(arr[k][0]); }
  return t; };
window.NF=Math.round(TOTAL*FPS);
