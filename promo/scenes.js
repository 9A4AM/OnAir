// OnAir promo v7: a calm, cinematic product film. Real-instrument visuals in the app's palette (single accent colour, navy waterfall
// ramp, accent-coloured constellations on near-black plots), flat hairline panels, light type, slow fades and dissolves only.
// Scene functions get bt = seconds since the scene's bar line. Every cue sits on the 120 BPM grid (see CUES, used by music.py).
Object.assign(C,{blue:'#5ca8db',green:'#66c285',amber:'#e6b347',red:'#e06157',ink:'#e6eaee',dim:'#8793a0',dim2:'#4d565f'});
const PAL={accent:'#5ca8db',ok:'#66c285',warn:'#e6b347',bad:'#e06157',grey:'#9eabba',plot:'#030405',border:'#363d45',panel:'#0c1014',faint:'#6c7782'};
const acc=a=>`rgba(92,168,219,${a})`;
let CURBT=0, CURT=0;
const eio3=k=>{k=clamp(k);return k<.5?4*k*k*k:1-Math.pow(-2*k+2,3)/2};
const ease=(bt,t0,d=.9)=>eio3(seg(bt,t0-.2,t0-.2+d*.6));   // fades start slightly early so the visual arrives on the beat

// ---------------------------------------------------------------- signal model
const NB=1024;
function expAvg(bin,f,M=6){ let a=0; for(let j=0;j<M;j++){ const u=Math.max(1e-6,hash(bin*1.7321+(f-j)*131.31)); a+=-Math.log(u); } return a/M; }
// real captures: signals.js holds Welch-averaged FFT rows of synthesised OFDM (DVB-T 8K 64-QAM, two DAB ensembles) through a moving multipath channel
const SD=(()=>{ const d=window.SIGDATA, un=b=>Uint8Array.from(atob(b),c=>c.charCodeAt(0)); return {nf:d.nf,nb:d.nb,db0:d.db0,db1:d.db1,T_s:un(d.T_s),T_n:un(d.T_n),D_s:un(d.D_s),D_n:un(d.D_n)}; })();
function dataDb(kind,u,t,kl,kr){
  const nf=SD.nf, f=((Math.floor(t*30)%nf)+nf)%nf, b=Math.min(SD.nb-1,Math.max(0,Math.round(u*(SD.nb-1)))), o=f*SD.nb+b, sc=(SD.db1-SD.db0)/255;
  const S=Math.pow(10,(SD[kind+'_s'][o]*sc+SD.db0)/10), N=Math.pow(10,(SD[kind+'_n'][o]*sc+SD.db0)/10);
  const k=u<.5?kl:kr; return 10*Math.log10(S*k*k+N);
}
function specDb(u,t,sigs){
  if(sigs.length&&sigs[0].dat){ const k0=sigs[0].k??1, k1=sigs.length>1?(sigs[1].k??1):k0; return dataDb(sigs[0].dat,u,t,k0,k1); }
  const f=Math.floor(t*30), bin=Math.min(NB-1,Math.max(0,Math.round(u*NB)));
  const breathe=.6*Math.sin(t*.35+1.3)+.3*Math.sin(t*.9), tilt=1.8*(.5-u);
  let P=Math.pow(10,(-86+tilt)/10)*expAvg(bin,f);                                   // thermal noise floor, video-averaged
  for(const s of sigs){ const k=(s.k??1); if(k<=.001) continue; const d=Math.abs(u-s.u)-s.w;
    const edge=d<=-.012?1:d>=.012?0:.5-.5*Math.sin((d/.012)*Math.PI/2);              // channel roll-off
    const shoulder=d>0?Math.pow(10,-4.1)*Math.exp(-d/.02):0;                         // adjacent-channel leakage
    const rip=.35*Math.sin(u*37+t*.3)+.25*Math.sin(u*93+1.7)+.4*vnoise(u*26+3);       // in-band ripple, dB
    const lvl=Math.pow(10,(-47+rip+breathe*.5)/10)*k*k;
    P+=lvl*(edge+shoulder)*expAvg(bin+4096,f); }
  P+=Math.pow(10,-7.2)*Math.exp(-Math.pow((u-.073)*NB/1.2,2)); P+=Math.pow(10,-6.9)*Math.exp(-Math.pow((u-.934)*NB/1.2,2));   // two stationary spurs
  return 10*Math.log10(P)+breathe*.15;
}
function floorDb(u,t){ return specDb(u,t,[]); }
function ofdm(u,t,c0,half,k=1){ return specDb(u,t,[{u:c0,w:half,k}]); }
const STOPS=[[6,10,20],[16,38,70],[30,100,140],[110,190,205],[240,250,250]];
function ramp(v){v=clamp(v)*4; const i=Math.min(3,Math.floor(v)), f=v-i; return STOPS[i].map((a,k)=>a+(STOPS[i+1][k]-a)*f);}
const wf=mk(420,170), wfx=wf.getContext('2d'), wfd=wfx.createImageData(420,170);
function waterfall(c,x,y,w,h,t,sigs,rowDt=.06){
  const d=wfd.data;
  for(let r=0;r<170;r++){const tau=t-r*rowDt; for(let i=0;i<420;i++){const v=(specDb(i/420,tau,sigs)+100)/68; const [R,G,B]=ramp(v); const o=(r*420+i)*4; d[o]=R;d[o+1]=G;d[o+2]=B;d[o+3]=255;}}
  wfx.putImageData(wfd,0,0); c.save(); c.imageSmoothingEnabled=true; c.imageSmoothingQuality='high'; c.drawImage(wf,x,y,w,h); c.restore();
}
function plotFrame(c,x,y,w,h){ c.fillStyle=PAL.plot; c.fillRect(x,y,w,h); c.strokeStyle=PAL.border; c.lineWidth=1.2; c.strokeRect(x+.6,y+.6,w-1.2,h-1.2); }
function spectrum(c,x,y,w,h,t,sigs,o={}){
  if(!o.noFrame) plotFrame(c,x,y,w,h); c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip();
  if(o.grid!==false){c.strokeStyle='rgba(153,163,179,.14)'; c.lineWidth=1; for(let i=1;i<10;i++){c.beginPath();c.moveTo(x+i*w/10,y);c.lineTo(x+i*w/10,y+h);c.stroke()} for(let i=1;i<6;i++){c.beginPath();c.moveTo(x,y+i*h/6);c.lineTo(x+w,y+i*h/6);c.stroke()}}
  const N=o.n||420, lo=-100, hi=-30, pts=[], pk=[];
  for(let i=0;i<=N;i++){const u=i/N, d=specDb(u,t,sigs); pts.push([x+u*w,y+h-clamp((d-lo)/(hi-lo))*h]); const p=specDb(u,t-.35,sigs)+3; pk.push([x+u*w,y+h-clamp((Math.max(d,p)-lo)/(hi-lo))*h]);}
  if(o.band!==false) for(const s of sigs){ const k=s.k??1; if(k>0.02){c.fillStyle=acc(.09*Math.min(1,k)); c.fillRect(x+(s.u-s.w)*w,y,2*s.w*w,h);} }
  c.strokeStyle='rgba(158,171,186,.34)'; c.lineWidth=1.3; c.beginPath(); pk.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke();
  c.strokeStyle=PAL.accent; c.lineWidth=o.lw||2; c.lineJoin='round'; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke();
  c.restore();
  if(o.axes){ const f0=o.f0??598, df=o.df??1; for(let i=0;i<=8;i++) txt(c,(f0+i*df)+'',x+(i+.5)*w/9.0,y+h+26,{size:o.fs||15,mono:true,col:C.dim,al:'center'}); if(o.db){ for(let i=0;i<=6;i++) txt(c,String(-30-i*12),x-10,y+h*(i/6)+5,{size:o.fs||14,mono:true,col:C.dim,al:'right'}); } }
}
function constel(c,x,y,s,M,sigma,t,o={}){
  plotFrame(c,x,y,s,s); const side=Math.sqrt(M), n=o.n||(M<=4?300:1400), cell=s/(side+.6); c.save(); c.beginPath(); c.rect(x+2,y+2,s-4,s-4); c.clip();
  const pos=i=>{ if(M===2) return [(i&1?1:-1)*.3*s,0]; if(M===4) return [(i&1?1:-1)*.2*s,((i>>1)&1?1:-1)*.2*s]; const a=i%side,b=Math.floor(i/side)%side; return [(a-(side-1)/2)*cell,(b-(side-1)/2)*cell]; };
  c.fillStyle=acc(o.a??.8); const ds=o.dot||Math.max(2,s/260);
  const fr=Math.floor(t*10), rot=.004*t, cr=Math.cos(rot), sr=Math.sin(rot);
  for(let k=0;k<n;k++){ const [px,py]=pos(Math.floor(hash(k*3.1+7)*M)); const kk=k+fr*7919; const sg=sigma*cell;
    const pa=gauss(kk+777)*.011; let ex=px+gauss(kk)*sg, ey=py+gauss(kk+9999)*sg; const ca_=Math.cos(pa), sa_=Math.sin(pa); const rx=ex*ca_-ey*sa_, ry=ex*sa_+ey*ca_;
    c.fillRect(x+s/2+rx*cr-ry*sr-ds/2,y+s/2+rx*sr+ry*cr-ds/2,ds,ds); }
  c.strokeStyle='rgba(255,255,255,.12)'; c.lineWidth=1; c.beginPath();
  if(M>4) for(let a=0;a<side;a++){const q=(a-(side-1)/2)*cell; c.moveTo(x+s/2+q,y+6);c.lineTo(x+s/2+q,y+s-6);c.moveTo(x+6,y+s/2+q);c.lineTo(x+s-6,y+s/2+q);}
  c.moveTo(x+s/2,y);c.lineTo(x+s/2,y+s);c.moveTo(x,y+s/2);c.lineTo(x+s,y+s/2);c.stroke(); c.restore();
}
function lamp(c,x,y,on,label,size=26){ c.fillStyle=on>0?PAL.ok:'#434a52'; c.beginPath(); c.arc(x,y,size*.3,0,7); c.fill(); if(label) txt(c,label,x+size*.65,y+size*.35,{size,w:500,col:on>0?C.ink:C.dim}); }

// ---------------------------------------------------------------- layout kit: flat hairline panels, light type, slow fades
function bgd(c,glow=.7){ c.fillStyle='#06080b'; c.fillRect(0,0,W,H);
  const g=c.createLinearGradient(0,0,0,H); g.addColorStop(0,'rgba(255,255,255,.014)'); g.addColorStop(1,'rgba(255,255,255,0)'); c.fillStyle=g; c.fillRect(0,0,W,H); }
function panel(c,x,y,w,h,o={}){ c.save(); rr(c,x,y,w,h,o.r??6); c.fillStyle=o.fill||PAL.panel; c.fill();
  rr(c,x+.5,y+.5,w-1,h-1,o.r??6); c.strokeStyle=o.active?'rgba(110,180,225,.55)':'rgba(255,255,255,.09)'; c.lineWidth=1; c.stroke(); c.restore(); }
function line(c,s,x,y,bt,t0,o={}){ const k=ease(bt,t0,o.dur??.9); if(k<=0) return 0; c.save(); c.globalAlpha*=k; c.font=`${o.w||400} ${o.size||30}px Inter`; c.letterSpacing=(o.ls??0)+'px'; c.textAlign='left'; c.fillStyle=o.col||C.ink; const tw=c.measureText(s).width; c.fillText(s,o.al==='center'?x-tw/2:o.al==='right'?x-tw:x,y+(1-k)*(o.dy??9)); c.restore(); return tw; }
function head(c,l1,l2,bt,t0,o={}){ const x=o.x??120,y=o.y??150,s=o.size??58; line(c,l1,x,y,bt,t0,{size:s,w:400,ls:-s*.018,al:o.al}); if(l2) line(c,l2,x,y+s*1.12,bt,t0+.25,{size:s,w:400,ls:-s*.018,col:PAL.accent,al:o.al}); }
function sub(c,s,bt,t0,y=222,x=120){ line(c,s,x,y,bt,t0,{size:26,w:300,col:C.dim,ls:.1}); }
function chip(c,s,cx,cy,a=1,o={}){ c.save(); c.globalAlpha*=a; c.font=`${o.w||400} ${o.size||22}px Inter`; const tw=c.measureText(s).width+(o.pad||40), th=(o.size||22)+24;
  rr(c,cx-tw/2,cy-th/2,tw,th,5); c.fillStyle='rgba(255,255,255,.025)'; c.fill(); c.strokeStyle=o.stroke||'rgba(255,255,255,.14)'; c.lineWidth=1; c.stroke();
  txt(c,s,cx,cy+(o.size||22)*.35,{size:o.size||22,w:o.w||400,al:'center',col:o.col||C.ink}); c.restore(); return tw; }
function fadeUp(c,bt,t0,fn,o={}){ const k=ease(bt,t0,o.dur??.9); if(k<=0) return; c.save(); c.globalAlpha*=k; c.translate(0,(1-k)*(o.dy??10)); fn(k); c.restore(); }
function wrapText(c,s,x,y,maxW,lh,col){ const words=s.split(' '); let ln='',yy=y; c.fillStyle=col; for(const w of words){ const t=ln?ln+' '+w:w; if(c.measureText(t).width>maxW&&ln){ c.fillText(ln,x,yy); ln=w; yy+=lh; } else ln=t; } c.fillText(ln,x,yy); }
function bez(a,b,c2,d,t){const u=1-t;return u*u*u*a+3*u*u*t*b+3*u*t*t*c2+t*t*t*d}
function persp(c,src,cx,cy,sc,angDeg,alpha=1){ const sw=src.width, sh=src.height, ang=angDeg*Math.PI/180, strip=3, f=2800; c.save(); c.globalAlpha*=alpha;
  for(let x=0;x<sw;x+=strip){ const lx=(x-sw/2)*sc, z=lx*Math.sin(ang), px=lx*Math.cos(ang), s=f/(f+z), h=sh*sc*s; c.drawImage(src,x,0,strip,sh,cx+px*s,cy-h/2,strip*sc*Math.cos(ang)*s+.6,h); } c.restore(); }
function check(c,x,y,r,col){ c.fillStyle='rgba(92,168,219,.16)'; c.beginPath(); c.arc(x,y,r,0,7); c.fill(); c.strokeStyle=col||PAL.accent; c.lineWidth=2.4; c.lineCap='round'; c.lineJoin='round'; c.beginPath(); c.moveTo(x-r*.42,y+r*.02); c.lineTo(x-r*.1,y+r*.36); c.lineTo(x+r*.46,y-r*.36); c.stroke(); }

// ---- broadcast test card with lower third (the picture the TV scene shows)
function tvPicture(c,x,y,w,h,t,o={}){
  c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip();
  const bars=['#bdbdbd','#bdbd00','#00bdbd','#00bd00','#bd00bd','#bd0000','#0000bd'], bw=w/7;
  bars.forEach((b,i)=>{c.fillStyle=b;c.fillRect(x+i*bw,y,bw+1,h*.66)});
  ['#0000bd','#0c0c0c','#bd00bd','#0c0c0c','#00bdbd','#0c0c0c','#bdbdbd'].forEach((b,i)=>{c.fillStyle=b;c.fillRect(x+i*bw,y+h*.66,bw+1,h*.08)});
  c.fillStyle='#0a0d14'; c.fillRect(x,y+h*.74,w,h*.26); c.fillStyle='#ececec'; c.fillRect(x+bw*1.1,y+h*.74,bw*1.5,h*.26); c.fillStyle='#2b1359'; c.fillRect(x+bw*2.7,y+h*.74,bw*1.5,h*.26);
  const tc=Math.floor(t*25)%25, ss=Math.floor(t)%60; rr(c,x+w*.03,y+h*.04,w*.17,h*.06,h*.008); c.fillStyle='rgba(0,0,0,.6)'; c.fill(); txt(c,`18:00:${String(ss).padStart(2,'0')}:${String(tc).padStart(2,'0')}`,x+w*.03+w*.085,y+h*.04+h*.042,{size:h*.032,mono:true,al:'center',col:'rgba(255,255,255,.9)'});
  const lt=ease(t,o.lt??.6,.9); if(!o.noLower){ c.save(); c.globalAlpha=lt; c.translate(-(1-lt)*w*.05,0); const bx=x+w*.04,by=y+h*.79,bw2=w*.46,bh=h*.14; c.fillStyle='rgba(8,12,18,.9)'; c.fillRect(bx,by,bw2,bh); c.fillStyle='#c9433a'; c.fillRect(bx,by,h*.011,bh);
    txt(c,'Channel One HD',bx+h*.035,by+bh*.47,{size:h*.047,w:600}); txt(c,'Evening News  18:00',bx+h*.035,by+bh*.82,{size:h*.031,col:'#b9c4cf',w:400}); c.restore(); }
  rr(c,x+w*.855,y+h*.04,w*.11,h*.06,h*.008); c.fillStyle='#b83a32'; c.fill(); txt(c,'ON AIR',x+w*.91,y+h*.04+h*.042,{size:h*.032,w:600,al:'center',col:'#fff'});
  c.fillStyle='rgba(0,0,0,.10)'; for(let yy=0;yy<h;yy+=4) c.fillRect(x,y+yy+2,w,2);
  const vg=c.createRadialGradient(x+w/2,y+h/2,h*.45,x+w/2,y+h/2,h*.95); vg.addColorStop(0,'rgba(0,0,0,0)'); vg.addColorStop(1,'rgba(0,0,0,.35)'); c.fillStyle=vg; c.fillRect(x,y,w,h);
  c.restore();
}

const T={};
// ================================================================ v2 (release 0.1.0): built around real footage of the app.
// The footage is the actual Linux build decoding a DVB-T test broadcast (see footage/README.md); image sequences live outside git.
const SRC={clips:'file:///tmp/clips2/', prog:'file:///tmp/progf/p_'};
const pad=(n,k)=>String(n).padStart(k,'0');
const CLIPN={overview:1200,Player:420,Guide:420,Outputs:420,echo:420,onny:300};
const clipSrc=(name,t)=>SRC.clips+name+'/f_'+pad(clamp(Math.floor(t*30),0,CLIPN[name]-1)+1,5)+'.jpg';
const progSrc=t=>SRC.prog+pad(((Math.floor(t*25)%500)+500)%500,4)+'.jpg';
// two-pass image loading: pass 1 records what a frame needs, the frame is drawn once everything is decoded
let NEED=null; const LOADED=new Map(), PENDING=new Map();
function img(src){ const im=LOADED.get(src); if(im) return im; if(NEED) NEED.add(src); return null; }
function loadImg(src){ if(PENDING.has(src)) return PENDING.get(src); const p=new Promise(res=>{ const i=new Image(); i.onload=()=>res(i); i.onerror=()=>res(null); i.src=src; }); PENDING.set(src,p); return p; }
async function ensure(srcs){ await Promise.all([...srcs].map(async s=>{ const i=await loadImg(s); PENDING.delete(s); if(i) LOADED.set(s,i); }));
  if(LOADED.size>260){ const ks=[...LOADED.keys()]; for(let k=0;k<LOADED.size-200;k++) LOADED.delete(ks[k]); } }
// ---- footage camera: the frame point (cx,cy) is centred on screen and magnified z times
function camMix(a,b,k){ return {cx:lerp(a.cx,b.cx,k),cy:lerp(a.cy,b.cy,k),z:lerp(a.z,b.z,k)}; }
function camFix(cm){ const hw=W/(2*cm.z), hh=H/(2*cm.z); return {cx:clamp(cm.cx,hw,W-hw),cy:clamp(cm.cy,hh,H-hh),z:cm.z}; }
function toScreen(cm,x,y){ return [W/2+(x-cm.cx)*cm.z, H/2+(y-cm.cy)*cm.z]; }
let FOOTBLUR=0;
function foot(c,src,cm,after){ cm=camFix(cm); const im=img(src); c.save(); c.fillStyle='#0b0c0e'; c.fillRect(0,0,W,H);
  c.translate(W/2,H/2); c.scale(cm.z,cm.z); c.translate(-cm.cx,-cm.cy); c.imageSmoothingQuality='high'; if(FOOTBLUR>.05) c.filter=`blur(${FOOTBLUR.toFixed(2)}px)`; if(im) c.drawImage(im,0,0,W,H); if(after) after(c,cm); c.filter='none'; c.restore(); return cm; }
// the decoded programme, drawn into the app's video rectangles at full frame rate (the capture machine could not show it smoothly)
const RECT_TV=[282,158,1004,565], RECT_THUMB=[1578,570,324,182];
function progInto(c,t,r,a=1){ const im=img(progSrc(t)); if(!im) return; c.save(); c.globalAlpha*=a; c.drawImage(im,r[0],r[1],r[2],r[3]); c.restore(); }
// ---- captions: a soft scrim and a two-line title, bottom-left (or top-left)
function caption(c,bt,t0,l1,l2,o={}){ const k=ease(bt,t0,1.0)*(o.t1!=null?1-ease(bt,o.t1,.6):1); if(k<=0) return; const top=!!o.top;
  c.save(); c.globalAlpha*=k;
  if(top){ const g=c.createLinearGradient(0,0,0,360); g.addColorStop(0,'rgba(5,7,10,.88)'); g.addColorStop(.6,'rgba(5,7,10,.55)'); g.addColorStop(1,'rgba(5,7,10,0)'); c.fillStyle=g; c.fillRect(0,0,W,360); }
  else { const g=c.createLinearGradient(0,H-380,0,H); g.addColorStop(0,'rgba(5,7,10,0)'); g.addColorStop(.5,'rgba(5,7,10,.72)'); g.addColorStop(1,'rgba(5,7,10,.92)'); c.fillStyle=g; c.fillRect(0,H-380,W,380); }
  const y=top?150:H-150, x=o.x??110;
  c.translate(0,(1-ease(bt,t0,1.0))*10);
  txt(c,l1,x,y,{size:o.size??64,w:500,ls:-1.4}); if(l2) txt(c,l2,x+2,y+54,{size:29,w:300,col:'#c2ccd6',a:ease(bt,t0+.25,1.0)});
  c.restore(); }
// ---- callout: a dot on the footage, a hairline and a small label
function callout(c,bt,t0,cm,fx,fy,label,dx,dy){ const k=ease(bt,t0,.8); if(k<=0) return; const [x,y]=toScreen(cm,fx,fy); const lx=x+dx, ly=y+dy;
  c.save(); c.globalAlpha*=k; c.strokeStyle='rgba(150,205,240,.9)'; c.lineWidth=1.6; c.beginPath(); c.moveTo(x,y); c.lineTo(lerp(x,lx,k),lerp(y,ly,k)); c.stroke();
  c.fillStyle='#9fd3f5'; c.beginPath(); c.arc(x,y,5,0,7); c.fill(); c.strokeStyle='rgba(159,211,245,.35)'; c.lineWidth=2; c.beginPath(); c.arc(x,y,11+4*Math.sin(bt*3),0,7); c.stroke();
  c.font='500 24px Inter'; const tw=c.measureText(label).width+32; const bx=dx<0?lx-tw:lx, by=ly-22; rr(c,bx,by,tw,44,8); c.fillStyle='rgba(8,12,17,.9)'; c.fill(); c.strokeStyle='rgba(150,205,240,.55)'; c.lineWidth=1.2; c.stroke();
  txt(c,label,bx+16,by+30,{size:24,w:500}); c.restore(); }
function pill(c,s,cx,cy,o={}){ c.save(); c.font=`500 ${o.size||26}px Inter`; const tw=c.measureText(s).width+56, th=(o.size||26)+26; rr(c,cx-tw/2,cy-th/2,tw,th,th/2);
  c.fillStyle=o.accent?'rgba(92,168,219,.14)':'rgba(255,255,255,.04)'; c.fill(); c.strokeStyle=o.accent?'rgba(120,190,235,.8)':'rgba(255,255,255,.2)'; c.lineWidth=1.4; c.stroke();
  txt(c,s,cx,cy+(o.size||26)*.36,{size:o.size||26,w:500,al:'center',col:o.accent?'#bfe3fb':C.ink}); c.restore(); }
// ImGui look-alike widgets in the app's own palette and font (for the parts the capture build does not have)
const UI={font:'DejaVu Sans', fs:14.5, text:'#e0e5ea', dim:'#7f8a95', frame:'#1d2126', frameHov:'#262b33', check:'#73c7ff', bg:'#141619', plot:'#030405', grid:'rgba(153,163,179,.2)', axis:'#adb8c4', line:'#5ca8db'};
function uiText(c,s,x,y,o={}){ c.save(); c.font=`${o.w||400} ${o.size||UI.fs}px ${o.mono?'DejaVu Sans Mono':UI.font}`; c.fillStyle=o.col||UI.text; c.textAlign=o.al||'left'; c.fillText(s,x,y); c.restore(); }
function uiCheck(c,x,y,label,on,hover){ rr(c,x,y,19,19,4); c.fillStyle=hover?UI.frameHov:UI.frame; c.fill();
  if(on){ c.save(); c.strokeStyle=UI.check; c.lineWidth=2.4; c.lineCap='round'; c.lineJoin='round'; c.beginPath(); c.moveTo(x+4.5,y+10); c.lineTo(x+8,y+13.5); c.lineTo(x+15,y+5.5); c.stroke(); c.restore(); }
  uiText(c,label,x+26,y+14.5); }
function cursor(c,x,y,press=0){ c.save(); c.translate(x,y); const s=1.25*(1-.12*press); c.scale(s,s); c.beginPath(); c.moveTo(0,0); c.lineTo(0,19); c.lineTo(4.6,14.6); c.lineTo(8,22); c.lineTo(11,20.8); c.lineTo(7.7,13.6); c.lineTo(14,13.6); c.closePath();
  c.shadowColor='rgba(0,0,0,.5)'; c.shadowBlur=6; c.shadowOffsetY=2; c.fillStyle='#fff'; c.fill(); c.shadowColor='transparent'; c.lineWidth=1.3; c.strokeStyle='#111'; c.stroke(); c.restore(); }

// ================================================================ v3: release 0.1.4. Real footage of the current app (see footage/README.md).
function lampsUi(c,x,y,names,on,sz=18){ let xx=x; names.forEach((n,i)=>{ const st=on[i]; c.fillStyle=st===1?'#62c37f':st===2?'#d9a441':'#4a5059'; c.beginPath(); c.arc(xx+sz*.4,y-sz*.32,sz*.36,0,7); c.fill(); c.save(); c.font=`${sz}px ${UI.font}`; c.fillStyle=st?UI.text:UI.dim; c.fillText(n,xx+sz*.95,y); xx+=sz*1.6+c.measureText(n).width; c.restore(); }); return xx; }
const VERSION='0.1.4';
// ---------------------------------------------------------------- 1. title
T.title=(c,bt)=>{
  bgd(c);
  const sg=[{dat:'T',u:.5,w:.38,k:ease(bt,1.6,2.0)}];
  const gw=eio3(seg(bt,.2,3.2))*W; c.save(); c.globalAlpha=.75*ease(bt,.1,1)*(1-.6*seg(bt,3.4,4.4)); c.strokeStyle=acc(.85); c.lineWidth=1.6; c.beginPath(); c.moveTo(W/2-gw/2,930); c.lineTo(W/2+gw/2,930); c.stroke(); c.restore();
  c.save(); c.globalAlpha=.55*ease(bt,1.4,2.2); spectrum(c,0,830,W,200,bt+3,sg,{n:500,lw:1.8,noFrame:true,grid:false,band:false}); c.restore();
  fadeUp(c,bt,4.0,()=>{ c.save(); c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=50; c.shadowOffsetY=22; c.drawImage(logo,W/2-130,150,260,260); c.restore(); },{dur:.5,dy:10});
  line(c,'OnAir',W/2,600,bt,4.5,{size:128,w:600,ls:-4,al:'center'});
  line(c,'Digital TV and radio from any SDR.',W/2,680,bt,5.5,{size:40,w:300,al:'center'});
  const k=ease(bt,6.5,.6); if(k>0){ c.save(); c.globalAlpha=k; c.translate(0,(1-k)*8); pill(c,'Version '+VERSION+'  ·  Out now',W/2,770,{accent:true,size:28}); c.restore(); }
};
// ---------------------------------------------------------------- 2. six standards lock in, one per beat
const LOCKS=[['DVB-T2','Europe · Asia · Africa','T2-Base · 32K · 256-QAM',[{dat:'T',u:.5,w:.38,k:1}],['P1','L1','Frame','LDPC']],
             ['DVB-T','Europe · older networks','8K · 64-QAM · 2/3',[{dat:'T',u:.5,w:.38,k:1}],['Sync','TPS','Viterbi','RS']],
             ['ATSC 1.0','North America · Korea','8-VSB · 6 MHz',[{u:.5,w:.3,k:1}],['Pilot','Field','Eq','RS']],
             ['ATSC 3.0','NextGen TV · experimental','OFDM · LDPC · HEVC',[{dat:'T',u:.5,w:.3,k:1}],['Boot','L1','LDPC','ROUTE']],
             ['ISDB-T','Japan · South America','13 segments · 64-QAM',[{dat:'T',u:.5,w:.33,k:1}],['Sync','TMCC','Viterbi','RS']],
             ['DAB+','Digital radio','Ensemble · 11 stations',[{dat:'D',u:.27,w:.128,k:1},{dat:'D',u:.72,w:.128,k:1}],['Sync','FIC','MSC','Audio']]];
T.standards=(c,bt)=>{
  bgd(c);
  head(c,'Every broadcast.','One receiver.',bt,.1,{x:120,y:140,size:60});
  const x0=120, pw=540, ph=330, gx=30, gy=28, y0=300;
  LOCKS.forEach(([nm,where,mode,sg,lamps],i)=>{ const L=1.0+i*1.0, x=x0+(i%3)*(pw+gx), y=y0+Math.floor(i/3)*(ph+gy);
    fadeUp(c,bt,.3+i*.12,()=>{ const locked=bt>=L, flash=locked?Math.exp(-(bt-L)*5):0;
      panel(c,x,y,pw,ph,{active:locked,fill:'#0c1014'}); if(flash>0){ c.save(); c.globalAlpha=flash*.6; rr(c,x,y,pw,ph,6); c.strokeStyle='#62c37f'; c.lineWidth=3; c.stroke(); c.restore(); }
      txt(c,nm,x+24,y+52,{size:38,w:600,ls:-.8}); txt(c,where,x+pw-24,y+50,{size:18,w:400,col:C.dim,al:'right'});
      spectrum(c,x+20,y+74,pw-40,150,bt+3+i*.7,sg,{n:240,lw:1.6,band:locked});
      if(nm==='ISDB-T'&&locked){ c.save(); c.strokeStyle='rgba(255,255,255,.18)'; c.lineWidth=1; const sx=x+20+(pw-40)*(.5-.33), sw=(pw-40)*.66; for(let k2=1;k2<13;k2++){ const xx=sx+sw*k2/13; c.beginPath(); c.moveTo(xx,y+80); c.lineTo(xx,y+218); c.stroke(); } c.restore(); }
      if(nm==='ATSC 1.0'){ c.save(); c.strokeStyle=UI.line; c.lineWidth=1.6; const px=x+20+(pw-40)*.205; c.beginPath(); c.moveTo(px,y+74+150*.62); c.lineTo(px,y+74+150*.18); c.stroke(); c.restore(); }
      const on=lamps.map((n,j)=>bt>=L+j*.05?1:bt>L-.8?2:0); const xe=lampsUi(c,x+24,y+262,lamps,on,17);
      c.save(); c.font=`19px ${UI.font}`; c.fillStyle=locked?'#62c37f':'#9aa4ae'; c.fillText(locked?'Locked':'searching…',x+pw-24-c.measureText(locked?'Locked':'searching…').width,y+262); c.restore();
      txt(c,locked?mode:'',x+24,y+306,{size:21,w:400,col:'#c2ccd6',a:ease(bt,L,.5)});
    },{dur:.6,dy:10}); });
};
// ---------------------------------------------------------------- 3. the app: it locks on (real footage)
const APP_OFF=10.15, THUMB_FROM=15.0;
T.app=(c,bt)=>{
  const t=APP_OFF+bt;
  const z0={cx:600,cy:70,z:2.5}, z1={cx:960,cy:540,z:1.0};
  let cm=camMix(z0,z1,eio3(seg(bt,2.5,5.2))); cm.z*=1+.010*Math.max(0,bt-5.2);
  FOOTBLUR=2.2*ease(bt,6.2,.8)*(1-ease(bt,10.8,.6));
  cm=foot(c,clipSrc('overview',t),cm,cc=>{ if(t>=THUMB_FROM) progInto(cc,t,RECT_THUMB,ease(t,THUMB_FROM,.5)); });
  FOOTBLUR=0;
  const lk=seg(bt,2.0,2.8); if(lk>0&&lk<1){ const [x0,y0]=toScreen(cm,10,48), [x1,y1]=toScreen(cm,1300,88); c.save(); c.globalCompositeOperation='lighter'; const g=c.createLinearGradient(lerp(x0,x1,lk)-200,0,lerp(x0,x1,lk)+40,0); g.addColorStop(0,'rgba(120,200,255,0)'); g.addColorStop(.8,'rgba(120,200,255,.22)'); g.addColorStop(1,'rgba(120,200,255,0)'); c.fillStyle=g; c.fillRect(x0,y0,x1-x0,y1-y0); c.restore(); }
  callout(c,bt,6.5,cm,330,58,'Lock status at a glance',170,170);
  callout(c,bt,7.0,cm,760,640,'Live spectrum and waterfall',190,-70);
  callout(c,bt,7.5,cm,1700,260,'Every channel, with what is on',-330,70);
  callout(c,bt,8.0,cm,1170,945,'Constellations',250,-150);
  caption(c,bt,5.5,'Meet OnAir.','It finds the signal, locks on and decodes. On its own.');
};
// ---------------------------------------------------------------- 4. live TV (real footage)
T.tv=(c,bt)=>{
  if(bt<4.5){ const t=1+bt; const cm=camMix({cx:784,cy:440,z:1.5},{cx:860,cy:500,z:1.15},eio3(seg(bt,0,4.5)));
    foot(c,clipSrc('Player',t),cm,cc=>{ progInto(cc,bt+3,RECT_TV); progInto(cc,bt+3,RECT_THUMB); }); }
  else { const t=2+bt; const cm=camMix({cx:700,cy:250,z:1.7},{cx:780,cy:280,z:1.55},eio3(seg(bt,4.5,8)));
    foot(c,clipSrc('Guide',t),cm,cc=>progInto(cc,bt+3,RECT_THUMB)); }
  caption(c,bt,.5,'Watch live TV.','Subtitles, teletext, programme guide and every audio track.');
};
// ---------------------------------------------------------------- 5. see the signal, find the echoes (real footage)
T.signal=(c,bt)=>{
  if(bt<3){ const t=20+bt; const cm=camMix({cx:1170,cy:940,z:2.6},{cx:1430,cy:940,z:2.2},eio3(seg(bt,0,3))); foot(c,clipSrc('overview',t),cm); }
  else { const t=bt-1; const cm=camMix({cx:1540,cy:300,z:1.6},{cx:420,cy:180,z:1.75},eio3(seg(bt,3.4,4.8))); foot(c,clipSrc('echo',t),cm,cc=>progInto(cc,bt+4,RECT_THUMB)); }
  caption(c,bt,.5,'See your signal.','Every constellation, a quality score and an echo detector.');
};
// ---------------------------------------------------------------- 6. smooth picture gaps: stutter, one click, smooth (real footage, the real option)
const STALLS=[[1.2,1.6],[2.5,3.3],[4.0,4.35],[4.75,5.6],[6.0,6.3],[6.6,7.95]];   // scene seconds; music.py ducks on the same windows
const GB=[1663,523], CLICK=7.95;
function stallState(bt){ let lost=0; for(const [a,b] of STALLS){ if(bt>=b) lost+=b-a; else if(bt>=a) return {frozen:true,at:a-lost,since:bt-a}; } return {frozen:false,at:bt-lost}; }
T.gaps=(c,bt)=>{
  const t=3+bt, on=bt>=8.0; const st=on?{frozen:false,at:bt}:stallState(bt);
  const pk=bt>=8?Math.exp(-(bt-8)*3.2)*Math.min(1,(bt-8)/.08):0; const cm={cx:1000+40*pk,cy:450-20*pk,z:1.2+.1*pk};
  foot(c,clipSrc('Player',t),cm,cc=>{
    progInto(cc,st.at+2,RECT_TV); progInto(cc,st.at+2,RECT_THUMB);
    if(st.frozen){ const [x,y,w,h]=RECT_TV; const im=img(progSrc(st.at+2));
      if(im&&st.since<.22){ for(let k=0;k<26;k++){ const bw=32+((hash(k*3+Math.floor(st.at*10))*4)|0)*16, sx=x+hash(k*7+st.at)*(w-bw), sy=y+hash(k*11+st.at)*(h-bw); cc.drawImage(im,(sx-x)/w*1280+18,(sy-y)/h*720+10,bw/w*1280,bw/h*720,sx,sy,bw,bw); } }
      cc.fillStyle=`rgba(0,0,0,${.25*clamp(st.since/.4)})`; cc.fillRect(x,y,w,h);
      if(st.since>.25){ const cx=x+w/2, cy=y+h/2; for(let k=0;k<12;k++){ const a=bt*7+k/12*Math.PI*2; cc.fillStyle=`rgba(230,240,250,${(k/12)*.9})`; cc.beginPath(); cc.arc(cx+Math.cos(a)*24,cy+Math.sin(a)*24,4,0,7); cc.fill(); } } }
    const sw=seg(bt,8.0,8.6); if(sw>0&&sw<1){ const [x,y,w,h]=RECT_TV; cc.save(); cc.beginPath(); cc.rect(x,y,w,h); cc.clip(); cc.globalCompositeOperation='lighter'; const gx=x-300+sw*(w+600); const g=cc.createLinearGradient(gx-180,0,gx+180,0); g.addColorStop(0,'rgba(150,210,255,0)'); g.addColorStop(.5,'rgba(150,210,255,.35)'); g.addColorStop(1,'rgba(150,210,255,0)'); cc.fillStyle=g; cc.fillRect(x,y,w,h); cc.restore(); }
    if(!on){ rr(cc,GB[0],GB[1],21,20,3); cc.fillStyle=bt>7.2?'#2b313a':'#23272d'; cc.fill(); }   // the real checkbox, unticked until the click
  });
  const ck=ease(bt,5.4,.5); if(ck>0){ const [tx,ty]=toScreen(camFix(cm),GB[0]+9,GB[1]+9); const k=eio3(seg(bt,5.6,7.3)); const x=lerp(900,tx,k)+Math.sin(k*Math.PI)*-60, y=lerp(1000,ty,k)-Math.sin(k*Math.PI)*40;
    const pr=bt>CLICK-.06&&bt<CLICK+.14?1:0; c.save(); c.globalAlpha*=ck*(1-ease(bt,10.2,.6)); cursor(c,x,y,pr);
    const rk=seg(bt,CLICK,CLICK+.5); if(rk>0&&rk<1){ c.strokeStyle=`rgba(160,215,250,${.8*(1-rk)})`; c.lineWidth=2; c.beginPath(); c.arc(tx,ty,8+rk*34,0,7); c.stroke(); } c.restore(); }
  { const k=ease(bt,.8,.6); if(k>0){ const x0=300, y0=1014, w=1320, h=22, tx=v=>x0+v/12*w; c.save(); c.globalAlpha=k;
      rr(c,x0-24,y0-58,w+48,h+92,12); c.fillStyle='rgba(8,11,15,.9)'; c.fill(); c.strokeStyle='rgba(255,255,255,.12)'; c.lineWidth=1; c.stroke();
      txt(c,'Stream',x0,y0-22,{size:20,w:500,col:'#9aa6b2'});
      const nGap=STALLS.filter(([a])=>bt>=a).length, fillK=seg(bt,8.0,9.0);
      const label= bt<8? (nGap?nGap+' drop-out'+(nGap>1?'s':'')+', picture frozen':'') : 'Repaired '+Math.min(STALLS.length,Math.ceil(fillK*STALLS.length))+' / '+STALLS.length+'  ·  picture never stops';
      txt(c,label,x0+w,y0-22,{size:20,w:600,al:'right',col:bt<8?'#e06157':'#7cc6f5'});
      rr(c,x0,y0,w,h,5); c.fillStyle='#1a1f26'; c.fill();
      const hd=Math.min(bt,12); c.save(); rr(c,x0,y0,w,h,5); c.clip(); c.fillStyle='#3f8f5c'; c.fillRect(x0,y0,tx(hd)-x0,h);
      STALLS.forEach(([a,b2])=>{ if(bt<a) return; const e=Math.min(b2,hd); const filled=bt>=8&&fillK*12>=a; c.fillStyle=filled?'#4aa3e0':'#e06157'; c.fillRect(tx(a),y0,tx(e)-tx(a),h); if(filled){ const sh=clamp((fillK*12-a)/1.2); c.fillStyle=`rgba(255,255,255,${.6*(1-sh)})`; c.fillRect(tx(a),y0,tx(e)-tx(a),h); } }); c.restore();
      c.fillStyle='#e8eef3'; c.fillRect(tx(hd)-1.5,y0-6,3,h+12); c.restore(); }
    const bk=ease(bt,8.6,.5); if(bk>0){ c.save(); c.globalAlpha=bk; const s_=1+.25*(1-eoBack(clamp((bt-8.4)/.5))); c.translate(330,240); c.scale(s_,s_); rr(c,-150,-36,300,72,36); c.fillStyle='rgba(30,90,55,.92)'; c.fill(); c.strokeStyle='#62c37f'; c.lineWidth=2; c.stroke();
      c.strokeStyle='#bff0cf'; c.lineWidth=4; c.lineCap='round'; c.beginPath(); c.moveTo(-118,0); c.lineTo(-106,12); c.lineTo(-86,-12); c.stroke(); txt(c,'Uninterrupted',14,10,{size:28,w:600,al:'center'}); c.restore(); } }
  caption(c,bt,.5,'Weak signal? The picture freezes.',null,{top:true,t1:7.2,size:58});
  caption(c,bt,8.4,'Smooth picture gaps.','Short drop-outs are repaired, the picture keeps moving.',{top:true});
};
// ---------------------------------------------------------------- 7. share it: every screen in the home lights up at once on the hit
const DEV=[['Phone','HLS in the browser',[300,360,150,270],'phone'],['Tablet','HLS in the browser',[300,690,300,210],'tablet'],['TV','Apple TV · AirPlay (macOS)',[1390,250,420,236],'tv'],['Laptop','VLC',[1450,620,300,188],'laptop'],['Media server','Plex · Jellyfin',[1080,830,260,146],'tv']];
function device(c,kind,r,onK,t,label,sub,a){ const [x,y,w,h]=r; c.save(); c.globalAlpha*=a; const bz=kind==='phone'?10:kind==='tablet'?14:8;
  rr(c,x-bz,y-bz,w+2*bz,h+2*bz,kind==='phone'?22:kind==='tablet'?18:6); c.fillStyle='#0b0c0f'; c.fill(); c.strokeStyle='rgba(255,255,255,.12)'; c.stroke();
  if(kind==='laptop'){ c.fillStyle='#1b1d22'; rr(c,x-40,y+h+bz,w+80,14,4); c.fill(); }
  if(kind==='tv'){ c.fillStyle='#0b0c0f'; c.fillRect(x+w/2-6,y+h+bz,12,26); c.fillRect(x+w/2-50,y+h+bz+24,100,6); }
  c.save(); rr(c,x,y,w,h,kind==='phone'?12:3); c.clip(); c.fillStyle='#020203'; c.fillRect(x,y,w,h);
  if(onK>0){ const im=img(progSrc(t)); if(im){ c.globalAlpha*=onK; if(kind==='phone'){ const sw=h*16/9; c.drawImage(im,x+w/2-sw/2,y,sw,h); } else c.drawImage(im,x,y,w,h); } }
  c.restore();
  if(onK>0){ c.save(); c.globalCompositeOperation='lighter'; const g=c.createRadialGradient(x+w/2,y+h/2,10,x+w/2,y+h/2,Math.max(w,h)); g.addColorStop(0,`rgba(170,140,200,${.18*onK})`); g.addColorStop(1,'rgba(170,140,200,0)'); c.fillStyle=g; c.fillRect(x-w,y-h,w*3,h*3); c.restore(); }
  txt(c,label,x+w/2,y+h+bz+(kind==='tv'?62:kind==='laptop'?46:34),{size:24,w:600,al:'center'}); txt(c,sub,x+w/2,y+h+bz+(kind==='tv'?88:kind==='laptop'?72:60),{size:18,w:400,al:'center',col:C.dim});
  c.restore(); }
T.share=(c,bt)=>{
  if(bt<3.5){ const t=2+bt; const cm=camMix({cx:500,cy:700,z:1.9},{cx:520,cy:690,z:1.75},eio3(seg(bt,0,3.5))); foot(c,clipSrc('Outputs',t),cm,cc=>progInto(cc,bt+20,RECT_THUMB));
    caption(c,bt,.4,'Share it.','A built-in network tuner for your whole home.'); return; }
  bgd(c);
  const pc=[760,380,400,225]; // the computer running OnAir, centre
  const kin=ease(bt,3.5,.6); c.save(); c.globalAlpha=kin; rr(c,pc[0]-12,pc[1]-12,pc[2]+24,pc[3]+24,8); c.fillStyle='#0b0c0f'; c.fill(); const im=img(clipSrc('Player',12)); if(im) c.drawImage(im,pc[0],pc[1],pc[2],pc[3]); progInto(c,bt+20,[pc[0]+RECT_TV[0]*pc[2]/W,pc[1]+RECT_TV[1]*pc[3]/H,RECT_TV[2]*pc[2]/W,RECT_TV[3]*pc[3]/H]);
  c.fillStyle='#0b0c0f'; c.fillRect(pc[0]+pc[2]/2-10,pc[1]+pc[3]+12,20,40); c.fillRect(pc[0]+pc[2]/2-70,pc[1]+pc[3]+48,140,8); txt(c,'OnAir',pc[0]+pc[2]/2,pc[1]+pc[3]+100,{size:26,w:600,al:'center'}); c.restore();
  const HIT=12.0; // scene seconds: everything switches on together on the hit
  DEV.forEach(([lb,sub,r,kind],i)=>{ const ap=ease(bt,4.5+i*.75,.5); if(ap<=0) return; const cx=r[0]+r[2]/2, cy=r[1]+r[3]/2, sx=pc[0]+pc[2]/2, sy=pc[1]+pc[3]/2;
    // the stream: dashes flowing from the computer to the device, faster as the build rises
    c.save(); c.globalAlpha=ap*.75; c.strokeStyle='rgba(120,190,235,.8)'; c.lineWidth=2.5; c.setLineDash([10,14]); c.lineDashOffset=-bt*(60+80*seg(bt,8,12)); c.beginPath(); c.moveTo(sx,sy); c.quadraticCurveTo((sx+cx)/2,Math.min(sy,cy)-80,cx,cy); c.stroke(); c.restore();
    device(c,kind,r,bt>=HIT?clamp((bt-HIT)/.15):0,bt+20+i,lb,sub,ap); });
  if(bt>=HIT&&bt<HIT+.15){ c.fillStyle=`rgba(255,255,255,${.35*(1-(bt-HIT)/.15)})`; c.fillRect(0,0,W,H); }
  caption(c,bt,4.0,'Every screen in your home.','Phones, tablets, VLC, Plex and Jellyfin. AirPlay on the Mac.',{top:true});
};
// ---------------------------------------------------------------- 8. any radio
const RADIOS=[['HackRF One / Pro','native driver'],['RTL-SDR','built in · DAB and narrow channels'],['Airspy','built in'],['BladeRF','built in'],['LimeSDR','built in'],['PlutoSDR','built in'],['USRP','built in'],['SDRplay','via SoapySDR']];
T.radios=(c,bt)=>{
  bgd(c);
  head(c,'Bring your own radio.',null,bt,.1,{y:150,size:60});
  RADIOS.forEach(([n,s],i)=>{ const x=120+(i%4)*430, y=300+Math.floor(i/4)*250; fadeUp(c,bt,.5+i*.5,()=>{ const on=bt>=.5+i*.5; panel(c,x,y,400,210,{active:i===0});
    c.save(); c.globalAlpha*=.9; c.fillStyle='#1b2028'; rr(c,x+28,y+34,150,60,8); c.fill(); for(let k=0;k<3;k++){ c.fillStyle=k===0?'#62c37f':'#3a414b'; c.beginPath(); c.arc(x+48+k*22,y+64,6,0,7); c.fill(); } c.fillStyle='#888'; c.fillRect(x+178,y+56,40,8); c.restore();
    txt(c,n,x+28,y+150,{size:32,w:600}); txt(c,s,x+28,y+184,{size:19,w:400,col:i===0?PAL.accent:C.dim}); },{dur:.6,dy:10}); });
};
// ---------------------------------------------------------------- 9. no radio yet? Onny's tour (real footage)
T.onny=(c,bt)=>{
  const t=1+bt*.9; const cm=camMix({cx:420,cy:960,z:2.35},{cx:420,cy:950,z:2.05},eio3(seg(bt,0,7)));
  foot(c,clipSrc('onny',t),cm);
  caption(c,bt,.6,'No radio yet?','Onny gives you the tour with a built-in demo signal.',{top:true});
};
// ---------------------------------------------------------------- 10. native on macOS, Windows and Linux
const OSES=[{name:'macOS',clip:'overview',ct:24},{name:'Windows',clip:'Player',ct:5},{name:'Linux',clip:'Guide',ct:5}];
function desktop(c,os,t,x,y,w,h){ // os desktop background + bars, drawn into the rect (x,y,w,h) at scale w/W
  c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip(); c.translate(x,y); c.scale(w/W,h/H);
  if(os==='macOS'){ const g=c.createLinearGradient(0,0,W,H); g.addColorStop(0,'#1b1f4a'); g.addColorStop(.5,'#5a2c78'); g.addColorStop(1,'#d0627a'); c.fillStyle=g; c.fillRect(0,0,W,H);
    for(let k=0;k<5;k++){ c.fillStyle=`rgba(255,255,255,${.035+k*.01})`; c.beginPath(); c.moveTo(0,H*(.35+k*.12)); for(let xx=0;xx<=W;xx+=40) c.lineTo(xx,H*(.35+k*.12)+Math.sin(xx/300+k*1.7+t*.2)*60); c.lineTo(W,H); c.lineTo(0,H); c.fill(); }
    c.fillStyle='rgba(20,20,26,.55)'; c.fillRect(0,0,W,30); txt(c,'OnAir',62,21,{size:15,w:600}); ['File','Edit','View','Window','Help'].forEach((s,i)=>txt(c,s,128+i*62,21,{size:15,w:400})); txt(c,'Sat 21:40',W-24,21,{size:15,al:'right'});
    rr(c,W/2-330,H-92,660,78,22); c.fillStyle='rgba(40,40,52,.45)'; c.fill(); c.strokeStyle='rgba(255,255,255,.18)'; c.stroke(); for(let k=0;k<8;k++){ const ix=W/2-300+k*78; if(k===3){ c.drawImage(logo,ix,H-84,62,62); c.fillStyle='#ddd'; c.beginPath(); c.arc(ix+31,H-17,2.5,0,7); c.fill(); } else { rr(c,ix+2,H-82,58,58,14); c.fillStyle=['#5a8fd8','#6fc27a','#e8a33d','#d65c5c','#8f6ad6','#4bb8c4','#c9c9c9','#e07b39'][k]; c.globalAlpha=.75; c.fill(); c.globalAlpha=1; } } }
  else if(os==='Linux'){ const g=c.createRadialGradient(W*.7,H*.3,0,W*.6,H*.5,W); g.addColorStop(0,'#2f5b73'); g.addColorStop(.5,'#1d3142'); g.addColorStop(1,'#0e1720'); c.fillStyle=g; c.fillRect(0,0,W,H);
    c.fillStyle='rgba(255,255,255,.04)'; for(let k=0;k<6;k++){ c.beginPath(); c.moveTo(W*(.1+k*.17),H); c.lineTo(W*(.35+k*.17),0); c.lineTo(W*(.42+k*.17),0); c.lineTo(W*(.17+k*.17),H); c.fill(); }
    c.fillStyle='#0b0b0c'; c.fillRect(0,0,W,34); txt(c,'Activities',22,23,{size:16,w:500}); txt(c,'Sat 21:40',W/2,23,{size:16,w:600,al:'center'}); txt(c,'⏻',W-26,23,{size:16,al:'right'}); }
  else { const g=c.createRadialGradient(W*.5,H*.62,0,W*.5,H*.62,W*.75); g.addColorStop(0,'#3a8fe0'); g.addColorStop(.35,'#1a4f9c'); g.addColorStop(1,'#071a3a'); c.fillStyle=g; c.fillRect(0,0,W,H);
    for(let k=0;k<3;k++){ c.strokeStyle=`rgba(140,200,255,${.12-k*.03})`; c.lineWidth=60-k*15; c.beginPath(); c.ellipse(W*.5,H*.75,W*(.3+k*.08),H*(.22+k*.06),0,Math.PI,2*Math.PI); c.stroke(); }
    c.fillStyle='rgba(28,30,36,.82)'; c.fillRect(0,H-56,W,56); rr(c,W/2-260,H-46,190,36,18); c.fillStyle='rgba(255,255,255,.08)'; c.fill(); txt(c,'Search',W/2-220,H-22,{size:16,col:'#bbb'});
    for(let k=0;k<6;k++){ const ix=W/2-40+k*56; if(k===2) c.drawImage(logo,ix,H-50,44,44); else { rr(c,ix+4,H-46,36,36,8); c.fillStyle=['#e8b33d','#3d8fe8','#c9c9c9','#2fa86f','#d65c5c','#8f6ad6'][k]; c.globalAlpha=.8; c.fill(); c.globalAlpha=1; } } c.fillStyle='#fff'; c.fillRect(W/2-40+2*56+14,H-5,16,3); txt(c,'21:40',W-30,H-24,{size:15,al:'right'}); }
  c.restore(); }
function appWindow(c,os,src,x,y,w,progT){ const h=w*9/16, tb=os==='Linux'?46:os==='macOS'?34:32, r=os==='Windows'?8:12; const im=img(src);
  c.save(); c.shadowColor='rgba(0,0,0,.55)'; c.shadowBlur=60; c.shadowOffsetY=24; rr(c,x,y,w,h+tb,r); c.fillStyle='#141619'; c.fill(); c.restore();
  c.save(); rr(c,x,y,w,h+tb,r); c.clip(); c.fillStyle=os==='Linux'?'#2b2b2e':os==='macOS'?'#26282c':'#1f1f22'; c.fillRect(x,y,w,tb);
  if(im){ c.drawImage(im,x,y+tb,w,h); const sc=w/W; const rr2=os==='Windows'?RECT_TV:RECT_THUMB; progInto(c,progT,[x+rr2[0]*sc,y+tb+rr2[1]*sc,rr2[2]*sc,rr2[3]*sc]); if(os==='Windows') progInto(c,progT,[x+RECT_THUMB[0]*sc,y+tb+RECT_THUMB[1]*sc,RECT_THUMB[2]*sc,RECT_THUMB[3]*sc]); }
  c.restore();
  c.save(); rr(c,x+.5,y+.5,w-1,h+tb-1,r); c.strokeStyle='rgba(255,255,255,.14)'; c.lineWidth=1; c.stroke(); c.restore();
  if(os==='macOS'){ ['#ff5f57','#febc2e','#28c840'].forEach((cl,i)=>{ c.fillStyle=cl; c.beginPath(); c.arc(x+20+i*20,y+tb/2,6.5,0,7); c.fill(); }); txt(c,'OnAir',x+w/2,y+tb/2+5,{size:14,w:600,al:'center',col:'#d8dbe0'}); }
  else if(os==='Linux'){ txt(c,'OnAir',x+w/2,y+tb/2+6,{size:16,w:700,al:'center',col:'#eee'}); [0,1,2].forEach(i=>{ const cx=x+w-26-i*34; c.fillStyle='rgba(255,255,255,.1)'; c.beginPath(); c.arc(cx,y+tb/2,12,0,7); c.fill(); c.strokeStyle='#ddd'; c.lineWidth=1.6; c.beginPath(); if(i===0){ c.moveTo(cx-4,y+tb/2-4); c.lineTo(cx+4,y+tb/2+4); c.moveTo(cx+4,y+tb/2-4); c.lineTo(cx-4,y+tb/2+4); } else if(i===1){ c.rect(cx-4,y+tb/2-4,8,8); } else { c.moveTo(cx-4,y+tb/2+3); c.lineTo(cx+4,y+tb/2+3); } c.stroke(); }); }
  else { c.drawImage(logo,x+12,y+7,18,18); txt(c,'OnAir',x+38,y+21,{size:13,w:400,col:'#ddd'}); const gx=x+w; c.strokeStyle='#ddd'; c.lineWidth=1.2; c.beginPath(); c.moveTo(gx-138,y+16); c.lineTo(gx-128,y+16); c.rect(gx-88,y+11,10,10); c.moveTo(gx-38,y+11); c.lineTo(gx-28,y+21); c.moveTo(gx-28,y+11); c.lineTo(gx-38,y+21); c.stroke(); }
}
function osSlide(c,i,bt,dx,a=1){ const os=OSES[i]; c.save(); c.globalAlpha*=a; c.translate(dx,0);
  desktop(c,os.name,bt,0,0,W,H);
  appWindow(c,os.name,clipSrc(os.clip,os.ct+bt*.5),300,206,1320,bt+5+i*3);
  const g=c.createLinearGradient(0,30,0,200); g.addColorStop(0,'rgba(0,0,0,.35)'); g.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g; c.fillRect(0,30,W,170);
  txt(c,'Native on '+os.name+'.',W/2,150,{size:72,w:600,al:'center',ls:-1.6}); c.restore(); }
T.platforms=(c,bt)=>{
  c.fillStyle='#000'; c.fillRect(0,0,W,H);
  if(bt<8){ // slides with a push transition on the beat (4 s, 8 s)
    const pos=bt<2.75?0: bt<3.25? eio3(seg(bt,2.75,3.25)) : bt<5.75?1: bt<6.25? 1+eio3(seg(bt,5.75,6.25)) : 2;
    for(let i=0;i<3;i++){ const d=i-pos; if(Math.abs(d)<1) osSlide(c,i,bt,d*W*1.04,1); }
    if(pos%1!==0){ c.save(); c.fillStyle=`rgba(0,0,0,${.25*Math.sin((pos%1)*Math.PI)})`; c.fillRect(0,0,W,H); c.restore(); }
  } else { // all three side by side, then the climax
    const k=eio3(seg(bt,8,8.7)); const tw=560, th=315, gap=48, x0=W/2-(3*tw+2*gap)/2, y0=380;
    for(let i=0;i<3;i++){ const os=OSES[i]; const fx=lerp(i===2?0:(i-2)*W*1.04,x0+i*(tw+gap),k), fy=lerp(0,y0,k), fw=lerp(W,tw,k), fh=lerp(H,th,k);
      if(i<2&&k<.02) continue; c.save(); rr(c,fx,fy,fw,fh,12*k); c.clip(); desktop(c,os.name,bt,fx,fy,fw,fh); const sc=fw/W; appWindow(c,os.name,clipSrc(os.clip,os.ct+bt*.5),fx+300*sc,fy+206*sc,1320*sc,bt+5+i*3); c.restore();
      txt(c,os.name,x0+i*(tw+gap)+tw/2,y0+th+60,{size:34,w:500,al:'center',a:ease(bt,8.4+i*.2,.5)}); }
    line(c,'Native on macOS, Windows and Linux.',W/2,250,bt,8.5,{size:62,w:600,al:'center',ls:-1.4});
  }
};
// ---------------------------------------------------------------- 11. finale: all of it, at once (real footage panels)
const FIN=[['Player',[282,158,1004,565],'prog'],['overview',[40,136,1510,650],null],['overview',[1060,830,860,220],null],['Guide',[0,150,1560,320],null],['echo',[950,150,800,420],null],['Outputs',[0,100,1100,560],null]];
T.finale=(c,bt)=>{
  bgd(c);
  const cols=[120,690,1260], rows=[200,560], w=540, h=320; const dim=1-.55*ease(bt,2.0,.7);
  FIN.forEach(([clip,sr,kind],i)=>{ const k=ease(bt,i*.12,.5); if(k<=0) return; const x=cols[i%3], y=rows[Math.floor(i/3)];
    c.save(); c.globalAlpha*=k*dim; c.translate(0,(1-k)*16); rr(c,x,y,w,h,10); c.save(); c.clip(); c.fillStyle='#0b0c0e'; c.fillRect(x,y,w,h);
    const sc=Math.max(w/sr[2],h/sr[3]), dw=sr[2]*sc, dh=sr[3]*sc;
    if(kind==='prog'){ const im=img(progSrc(bt+6)); if(im) c.drawImage(im,x,y,w,h); }
    else { const t=clip==='overview'?24+bt:4+bt; const im=img(clipSrc(clip,t)); if(im) c.drawImage(im,sr[0],sr[1],sr[2],sr[3],x+(w-dw)/2,y+(h-dh)/2,dw,dh); }
    c.restore(); rr(c,x+.5,y+.5,w-1,h-1,10); c.strokeStyle='rgba(255,255,255,.12)'; c.stroke(); c.restore(); });
  const k=ease(bt,2.0,.9); if(k>0){ c.save(); c.globalAlpha=k; const bg=c.createLinearGradient(0,330,0,760); bg.addColorStop(0,'rgba(5,7,10,0)'); bg.addColorStop(.5,'rgba(5,7,10,.8)'); bg.addColorStop(1,'rgba(5,7,10,0)'); c.fillStyle=bg; c.fillRect(0,330,W,430);
    txt(c,'All of it. At once.',W/2,560+(1-k)*10,{size:112,w:600,ls:-3,al:'center'}); c.restore(); }
  const ps=['DVB-T2','DVB-T','ATSC 1.0','ATSC 3.0','ISDB-T','DAB+'], ws=[150,130,150,150,140,120]; let cx0=W/2-(ws.reduce((a,b)=>a+b,0)+100)/2;
  ps.forEach((s,i)=>{ chip(c,s,cx0+ws[i]/2,650,ease(bt,2.5+i*.25,.6),{size:22,pad:38}); cx0+=ws[i]+20; });
};
// ---------------------------------------------------------------- 12. outro on the TV, final hit on the link
T.outro=(c,bt)=>{
  bgd(c);
  const tw=1120, th=630, tx=W/2-tw/2, ty=90-14*ease(bt,5.6,.8);
  c.save(); c.globalCompositeOperation='lighter'; const g=c.createRadialGradient(W/2,ty+th/2,200,W/2,ty+th/2,1000); g.addColorStop(0,'rgba(170,130,190,.20)'); g.addColorStop(1,'rgba(170,130,190,0)'); c.fillStyle=g; c.fillRect(0,0,W,H); c.restore();
  c.save(); c.shadowColor='rgba(0,0,0,.7)'; c.shadowBlur=70; rr(c,tx-14,ty-14,tw+28,th+28,12); c.fillStyle='#08090b'; c.fill(); c.restore();
  const im=img(progSrc(bt+14)); if(im) c.drawImage(im,tx,ty,tw,th);
  const bb=ease(bt,.3,.5); if(bb>0){ c.save(); c.globalAlpha=bb; rr(c,tx+24,ty+24,300,46,6); c.fillStyle='rgba(8,12,18,.8)'; c.fill(); c.fillStyle='#4fb36a'; c.beginPath(); c.arc(tx+50,ty+47,6,0,7); c.fill(); txt(c,'LIVE · Channel One HD',tx+66,ty+55,{size:20,w:600}); c.restore(); }
  fadeUp(c,bt,.5,()=>{ c.drawImage(logo,W/2-430,ty+th+56,92,92); txt(c,'OnAir '+VERSION,W/2-318,ty+th+130,{size:82,w:600,ls:-2.5}); },{dur:.8});
  const k=ease(bt,1.5,.6); if(k>0){ c.save(); c.globalAlpha=k; pill(c,'Out now',W/2+330,ty+th+104,{accent:true,size:34}); c.restore(); }
  line(c,'Free and open source. macOS, Windows and Linux.',W/2,ty+th+210,bt,2.5,{size:30,w:300,col:'#c2ccd6',al:'center'});
  const gk=ease(bt,6.0,.35); if(gk>0){ c.save(); c.globalAlpha=gk; const s_=1+.12*(1-gk); c.translate(W/2,ty+th+300); c.scale(s_,s_); txt(c,'github.com/adswill/OnAir',0,0,{size:58,w:600,ls:-.6,col:'#8cc8ff',al:'center'}); c.restore(); }
};
// ---------------------------------------------------------------- timeline (120 BPM: one bar = 2 s) and cue grid for music.py
const B=[['title',0,10,null,0],['standards',10,18,'dissolve',.5],['app',18,30,'cut',0],['tv',30,38,'dissolve',.5],['signal',38,44,'dissolve',.5],['gaps',44,56,'dissolve',.5],['share',56,72,'dissolve',.5],['radios',72,78,'dissolve',.5],['onny',78,86,'dissolve',.5],['platforms',86,96,'dissolve',.5],['finale',96,104,'cut',0],['outro',104,116,'dissolve',.6]];
// hits: 4 title, 18 the app, 52 smooth gaps on, 68 every screen on, 96 all of it, 110 the link
const HITS_T=[4,18,52,68,96,110];
const BUILDS=[[0,4],[14,18],[48,52],[64,68],[88,96]];
const CUES=[5.5,6.5, 11.0,12.0,13.0,14.0,15.0,16.0, 20.0,23.5,24.5,25.0,25.5,26.0, 30.5,34.5, 38.5,41.5, 44.5,52.5, 56.5,60.5,61.25,62.0,62.75,63.5, 72.5,73.0,73.5,74.0,74.5,75.0,75.5,76.0, 78.5, 86.5,90.0,94.0, 98.0,98.5,99.0,99.5,100.0,100.5, 104.5,105.5,106.5];
const SCENES=B.map((s,i)=>{ const next=B[i+1]; const t0=s[1]-(s[4]||0)/2; const t1=s[2]+(next?next[4]/2:0); const o=[s[0],t1-t0,s[3],s[4]||0]; o.t0=t0; o.t1=t1; o.start=s[1]; o.idx=i; return o; });
const TOTAL=116; window.TOTAL=TOTAL; window.NF=Math.round(TOTAL*FPS);
window.SCENES=SCENES.map(s=>({name:s[0],t0:s.t0,t1:s.t1,tr:s[2],trd:s[3]}));
const bufA=mk(), bufB=mk(), ca=bufA.getContext('2d'), cb=bufB.getContext('2d');
function sceneAt(c,s,t){ c.clearRect(0,0,W,H); const bt=t-s.start; CURBT=bt; CURT=t; c.save(); T[s[0]](c,bt); c.restore(); }
const MB=new Set(['app','tv','signal','platforms','onny']);   // camera moves get a short shutter (3 sub-frames)
const mbA=mk(), mbx=mbA.getContext('2d');
function sceneMB(c,s,t){ if(!MB.has(s[0])){ sceneAt(c,s,t); return; } sceneAt(c,s,t); mbx.clearRect(0,0,W,H);
  for(const [dt,al] of [[-1/90,1/2],[-2/90,1/3]]){ sceneAt(mbx,s,t+dt); c.save(); c.globalAlpha=al; c.drawImage(mbA,0,0); c.restore(); } }
function composite(t){
  const act=SCENES.filter(s=>t>=s.t0&&t<s.t1);
  if(act.length===1){ sceneMB(ctx,act[0],t); return; }
  const A=act[0],Bn=act[1], p=clamp((t-Bn.t0)/Bn[3]); sceneAt(ca,A,t); sceneAt(cb,Bn,t);
  ctx.fillStyle='#000'; ctx.fillRect(0,0,W,H); ctx.drawImage(bufA,0,0); ctx.save(); ctx.globalAlpha=eio3(p); ctx.drawImage(bufB,0,0); ctx.restore();
}
// ---------------------------------------------------------------- post: hit punch, build line, vignette, fine grain, fades
const grain=mk(256,256), gx_=grain.getContext('2d'); { const id=gx_.createImageData(256,256); for(let i=0;i<256*256;i++){ const v=(hash(i*1.37)*255)|0; id.data[i*4]=id.data[i*4+1]=id.data[i*4+2]=v; id.data[i*4+3]=255; } gx_.putImageData(id,0,0); }
const pun=mk(), punx=pun.getContext('2d');
function post(t,frame){
  let pk=0; for(const h of HITS_T){ if(t>=h&&t<h+1) pk=Math.max(pk,Math.exp(-(t-h)*7)); }
  if(pk>.01){ punx.clearRect(0,0,W,H); punx.drawImage(cv,0,0); const z=1+.02*pk; ctx.save(); ctx.fillStyle='#05070a'; ctx.fillRect(0,0,W,H); ctx.translate(W/2,H/2); ctx.scale(z,z); ctx.translate(-W/2,-H/2); ctx.drawImage(pun,0,0); ctx.restore();
    ctx.save(); ctx.globalCompositeOperation='lighter'; ctx.globalAlpha=.06*pk; ctx.fillStyle='#9cc6e6'; ctx.fillRect(0,0,W,H); ctx.restore(); }
  for(const [a,b] of BUILDS){ if(t>=a&&t<b+.25){ const k=t<b?Math.pow((t-a)/(b-a),2.2):1, rel=t<b?1:1-seg(t,b,b+.25); ctx.save(); ctx.globalAlpha=.75*rel; ctx.strokeStyle='rgba(120,185,230,.9)'; ctx.lineWidth=2; ctx.beginPath(); ctx.moveTo(W/2-W*k/2,H-4); ctx.lineTo(W/2+W*k/2,H-4); ctx.stroke(); ctx.restore(); } }
  const g=ctx.createRadialGradient(W/2,H/2,H*.55,W/2,H/2,H*1.05); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.22)'); ctx.fillStyle=g; ctx.fillRect(0,0,W,H);
  ctx.save(); ctx.globalAlpha=.022; ctx.globalCompositeOperation='overlay'; const ox=(hash(frame*1.7)*256)|0, oy=(hash(frame*2.3)*256)|0; for(let x=-ox;x<W;x+=256) for(let y=-oy;y<H;y+=256) ctx.drawImage(grain,x,y); ctx.restore();
  const o=seg(t,TOTAL-1.4,TOTAL); if(o>0){ctx.fillStyle=`rgba(0,0,0,${o})`;ctx.fillRect(0,0,W,H)}
  const f=1-seg(t,0,.8); if(f>0){ctx.fillStyle=`rgba(0,0,0,${f})`;ctx.fillRect(0,0,W,H)}
}
window.ready=Promise.all([document.fonts.load('300 20px Inter'),document.fonts.load('400 20px Inter'),document.fonts.load('500 20px Inter'),document.fonts.load('600 20px Inter'),document.fonts.load('700 20px Inter'),document.fonts.load('500 20px "JetBrains Mono"'),document.fonts.load('400 14px "DejaVu Sans"'),document.fonts.load('700 14px "DejaVu Sans"'),logo.decode()]);
window.renderFrame=async(i)=>{ const t=i/FPS; NEED=new Set(); composite(t); const need=NEED; NEED=null; if(need.size){ await ensure(need); composite(t); } post(t,i); return t; };
