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
const RECT_TV=[457,190,968,544], RECT_THUMB=[1600,584,302,170];
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

// ================================================================ v4: release 0.2.0. Real footage of 0.2.0 (and of 0.1.4 for the before/after).
const VERSION='0.2.0';
const C3N={dvbt_lock:960,dvbt_tv:360,dvbt_hover:240,dvbt_click:300,atsc3_tv:270,isdbt_tv:270,dvbs:270,dtmb:270,atv:270,atv_video:270,dab:270,dab_radio:270,fm:270,drm:270,drm_radio:270,adsb:270,adsb_air:270,gnss:270,gnss_sky:270,dmr:270,atsc3:270,isdbt:270};
const c3=(n,t)=>'file:///tmp/clips3/'+n+'/f_'+pad(clamp(Math.floor(t*30),0,C3N[n]-1)+1,5)+'.jpg';
const c2=(n,t)=>'file:///tmp/clips2/'+n+'/f_'+pad(clamp(Math.floor(t*30),0,(n==='overview'?1200:420)-1)+1,5)+'.jpg';
const TV3=[457,190,968,544], TH3=[1600,584,302,170];
const HOVER=(window.HOVERX||[]);
// ---- kinetic type: words rise out of a mask one after another
function kt(c,s,x,y,bt,t0,o={}){ const size=o.size||64, w=o.w||600, words=s.split(' '), st=o.stagger??.07; c.save(); c.font=`${w} ${size}px Inter`; c.letterSpacing=(o.ls??-size*.02)+'px';
  const sp=c.measureText(' ').width, ws=words.map(t=>c.measureText(t).width), tot=ws.reduce((a,b)=>a+b,0)+sp*(words.length-1);
  let xx=o.al==='center'?x-tot/2:o.al==='right'?x-tot:x; const out=o.t1!=null?ease(bt,o.t1,.5):0;
  words.forEach((wd,i)=>{ const k=eio3(seg(bt,t0+i*st,t0+i*st+.45)); if(k>0){ c.save(); c.beginPath(); c.rect(xx-4,y-size*1.05,ws[i]+8,size*1.4); c.clip();
      c.globalAlpha*=(1-out); c.fillStyle=(o.acc&&o.acc.includes(i))?PAL.accent:(o.col||C.ink); c.fillText(wd,xx,y+(1-k)*size*1.1-out*size*.4); c.restore(); }
    xx+=ws[i]+sp; }); c.restore(); }
// ---- a footage card: frame src cropped to (sx,sy,sw,sh) into a rounded rect
function card(c,src,sr,x,y,w,h,o={}){ const im=img(src); c.save(); if(o.shadow!==false){ c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=40; c.shadowOffsetY=18; } rr(c,x,y,w,h,o.r??10); c.fillStyle='#0b0c0e'; c.fill(); c.restore();
  c.save(); rr(c,x,y,w,h,o.r??10); c.clip(); if(im){ const sc=Math.max(w/sr[2],h/sr[3]); c.drawImage(im,sr[0],sr[1],sr[2],sr[3],x+(w-sr[2]*sc)/2,y+(h-sr[3]*sc)/2,sr[2]*sc,sr[3]*sc); } if(o.after) o.after(c); c.restore();
  c.save(); rr(c,x+.5,y+.5,w-1,h-1,o.r??10); c.strokeStyle=o.stroke||'rgba(255,255,255,.14)'; c.lineWidth=1.2; c.stroke(); c.restore(); }
function label(c,s,sub,x,y,k=1){ if(k<=0) return; c.save(); c.globalAlpha*=k; c.font='600 30px Inter'; const tw=Math.max(c.measureText(s).width, sub?(c.font='400 19px Inter',c.measureText(sub).width):0)+40; rr(c,x,y-44,tw,sub?78:54,8); c.fillStyle='rgba(6,9,13,.86)'; c.fill(); c.strokeStyle='rgba(120,190,235,.5)'; c.stroke();
  txt(c,s,x+20,y-8,{size:30,w:600}); if(sub) txt(c,sub,x+20,y+20,{size:19,w:400,col:'#aeb8c2'}); c.restore(); }
// the 0.2.0 picture: the decoded programme in the new UI's video rectangles
function prog3(c,t,tv=true){ if(tv) progInto(c,t,TV3); progInto(c,t,TH3); }

// ---------------------------------------------------------------- 1. title: the version rolls from 0.1.0 to 0.2.0 on the first hit
const ROLL=[[0,'0.1.0'],[1.0,'0.1.1'],[1.5,'0.1.2'],[2.0,'0.1.3'],[2.5,'0.1.4'],[4.0,'0.2.0']];
T.title=(c,bt)=>{
  bgd(c);
  const sg=[{dat:'T',u:.5,w:.38,k:ease(bt,1.6,2.0)}];
  c.save(); c.globalAlpha=.5*ease(bt,1.2,2.2)*(1-.4*seg(bt,4,5)); spectrum(c,0,840,W,200,bt+3,sg,{n:500,lw:1.8,noFrame:true,grid:false,band:false}); c.restore();
  let i=0; while(i<ROLL.length-1&&bt>=ROLL[i+1][0]) i++; const v=ROLL[i][1], since=bt-ROLL[i][0], big=v==='0.2.0';
  const shake=bt>2.8&&bt<4?Math.sin(bt*90)*3*seg(bt,2.8,4):0;
  const yy=big?560:600, size=big?250:150;
  const k=eio3(clamp(since/.22)), prev=i>0?ROLL[i-1][1]:null;
  c.save(); c.translate(shake,0);
  if(prev&&k<1){ c.save(); c.globalAlpha*=1-k; txt(c,prev,W/2,yy+(-k*70),{size:150,w:600,al:'center',col:'#6b7480',ls:-6}); c.restore(); }
  c.save(); c.globalAlpha*=ease(bt,.2,.6); if(big){ c.shadowColor='rgba(92,168,219,.55)'; c.shadowBlur=60*Math.exp(-since*1.2)+20; }
  txt(c,v,W/2,yy+(1-k)*70,{size,w:700,al:'center',col:big?'#ffffff':'#7d8691',ls:-size*.04}); c.restore(); c.restore();
  txt(c,'OnAir',W/2,big?255:400,{size:big?54:44,w:600,al:'center',col:big?C.ink:'#7d8691',a:ease(bt,.3,.6)});
  if(big){ fadeUp(c,bt,4.0,()=>{ c.save(); c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=40; c.drawImage(logo,W/2-55,70,110,110); c.restore(); },{dur:.4,dy:8}); }
  kt(c,'The biggest update yet.',W/2,700,bt,5.5,{size:52,w:500,al:'center',acc:[1,2]});
  ['New interface','14 modes','Native radios','Much faster'].forEach((s,j)=>{ const kk=ease(bt,6.5+j*.5,.5); if(kk>0){ c.save(); c.globalAlpha*=kk; c.translate(0,(1-kk)*12); pill(c,s,W/2-570+j*380,800,{accent:j===0,size:26}); c.restore(); } });
};
// ---------------------------------------------------------------- 2. a new look: 0.1.4 wiped away by 0.2.0
T.look=(c,bt)=>{
  bgd(c);
  const z=lerp(.74,1.0,eio3(seg(bt,6,10))); const w=W*z, h=H*z, x=(W-w)/2, y=(H-h)/2+lerp(70,0,eio3(seg(bt,6,10)));
  const wipe=eio3(seg(bt,2.0,3.4)), ex=x+w*wipe;
  card(c,c3('dvbt_lock',4.4),[0,0,W,H],x,y,w,h,{r:lerp(14,0,seg(bt,8.5,10))});
  if(wipe<1){ c.save(); c.beginPath(); c.rect(ex,y-2,x+w-ex+4,h+4); c.clip(); card(c,c2('overview',6+bt*.2),[0,0,W,H],x,y,w,h,{r:14,shadow:false}); c.restore(); }
  if(wipe>0&&wipe<1){ c.save(); c.globalCompositeOperation='lighter'; const g=c.createLinearGradient(ex-120,0,ex+30,0); g.addColorStop(0,'rgba(120,200,255,0)'); g.addColorStop(.85,'rgba(170,220,255,.55)'); g.addColorStop(1,'rgba(255,255,255,0)'); c.fillStyle=g; c.fillRect(ex-120,y,150,h); c.fillStyle='rgba(255,255,255,.9)'; c.fillRect(ex-1.5,y,3,h); c.restore(); }
  const lk=1-ease(bt,6.2,.5); if(lk>0){ c.save(); c.globalAlpha*=lk; const old=wipe<.5; pill(c,old?'0.1.4':'0.2.0',x+120,y-40,{accent:!old,size:26}); c.restore(); }
  kt(c,'A whole new look.',W/2,110,bt,.3,{size:64,w:600,al:'center',acc:[2,3],t1:6.0});
};
// ---------------------------------------------------------------- 3. the new UI, live (real footage): lock, sidebar, readout, click to tune
const LOCK_OFF=4.4;
T.newui=(c,bt)=>{
  let src, cm;
  if(bt<8.5){ const t=LOCK_OFF+bt; src=c3('dvbt_lock',t);
    const K=[[0,{cx:960,cy:540,z:1}],[2.6,{cx:960,cy:540,z:1.02}],[3.6,{cx:150,cy:280,z:2.1}],[5.6,{cx:160,cy:300,z:2.0}],[6.4,{cx:300,cy:40,z:2.3}],[8.5,{cx:320,cy:45,z:2.2}]];
    let i=0; while(i<K.length-2&&bt>K[i+1][0]) i++; cm=camMix(K[i][1],K[i+1][1],eio3(seg(bt,K[i][0],K[i+1][0])));
    cm=foot(c,src,cm,cc=>{ if(t>=12) prog3(cc,t,false); });
    const lk=seg(bt,2.0,2.8); if(lk>0&&lk<1){ const [x0,y0]=toScreen(cm,300,74), [x1,y1]=toScreen(cm,1580,118); c.save(); c.globalCompositeOperation='lighter'; const g=c.createLinearGradient(lerp(x0,x1,lk)-200,0,lerp(x0,x1,lk)+40,0); g.addColorStop(0,'rgba(120,200,255,0)'); g.addColorStop(.8,'rgba(120,200,255,.25)'); g.addColorStop(1,'rgba(120,200,255,0)'); c.fillStyle=g; c.fillRect(x0,y0,x1-x0,y1-y0); c.restore(); }
    callout(c,bt,4.0,cm,90,240,'Every mode, one click away',260,40);
    callout(c,bt,6.9,cm,170,36,'The frequency, big and clear',300,130);
    kt(c,'Meet the new OnAir.',110,H-110,bt,.4,{size:64,w:600,t1:3.2});
  } else { const t=bt-8.5+.9; src=c3('dvbt_hover',t); cm={cx:1050,cy:560,z:1.35};
    cm=foot(c,src,cm,cc=>prog3(cc,t+20,false));
    const HV=window.HOVERX||[]; const f=clamp(Math.floor(t*30),0,HV.length-1), hx=HV[f]; if(hx>0){ const mx=hx<1400?hx-18:1300; const [sx,sy]=toScreen(cm,mx,640); cursor(c,sx,sy,0); }
    kt(c,'Click the waterfall to tune.',110,H-110,bt,8.7,{size:58,w:600,acc:[1]});
  }
};
// ---------------------------------------------------------------- 4. from 3 standards to 14 modes, then the montage
const MONT=[['DVB-T2 / DVB-T','Europe, Asia, Africa','dvbt_tv',{cx:940,cy:470,z:1.55}],['ATSC 3.0','NextGen TV','atsc3_tv',{cx:900,cy:430,z:1.5}],['ISDB-T','Japan, Brazil','isdbt_tv',{cx:900,cy:430,z:1.5}],
  ['DVB-S / S2','Satellite','dvbs',{cx:950,cy:420,z:1.3}],['DTMB','China, Hong Kong','dtmb',{cx:950,cy:420,z:1.3}],['Analog TV','PAL, SECAM, NTSC','atv_video',{cx:700,cy:400,z:1.6}],
  ['DAB / DAB+','Digital radio','dab_radio',{cx:950,cy:420,z:1.3}],['FM','Stereo and RDS','fm',{cx:950,cy:420,z:1.3}],['DRM','Shortwave to VHF','drm_radio',{cx:900,cy:420,z:1.4}],
  ['ADS-B','Aircraft, 1090 MHz','adsb_air',{cx:900,cy:420,z:1.4}],['GNSS','GPS, GLONASS, BeiDou','gnss_sky',{cx:900,cy:420,z:1.4}],['DMR','Digital voice','dmr',{cx:950,cy:420,z:1.3}]];
const MSLOT=(()=>{ const s=[]; let t=4; for(let i=0;i<12;i++){ const d=i<6?1.0:.5; s.push([t,t+d]); t+=d; } return s; })();   // 4..13 s
function montFrame(c,i,bt,cmOverride){ const [nm,sub,clip,cm0]=MONT[i]; const t=3+bt*.8+i*.3; const cm=cmOverride||{...cm0,z:cm0.z*(1+.03*(bt%1))};
  foot(c,c3(clip,t),cm,cc=>{ if(clip==='dvbt_tv') prog3(cc,t+30); }); }
T.modes=(c,bt)=>{
  if(bt<4){ bgd(c);
    txt(c,'In 0.1.0',W/2,250,{size:34,w:500,al:'center',col:C.dim,a:ease(bt,.2,.5)});
    const n=bt<2?3:Math.min(14,3+Math.floor((bt-2)/.07)); const big=n===14;
    c.save(); c.globalAlpha*=ease(bt,.2,.5); if(big){ c.shadowColor='rgba(92,168,219,.6)'; c.shadowBlur=50; } txt(c,String(n),W/2,560,{size:300,w:700,al:'center',col:big?'#fff':C.ink,ls:-10}); c.restore();
    txt(c,bt<2.6?'standards':'modes',W/2,650,{size:44,w:400,al:'center',col:big?PAL.accent:C.dim,a:ease(bt,.4,.5)});
    if(bt<2.4){ ['DVB-T2','DVB-T','ATSC'].forEach((s,j)=>chip(c,s,W/2-200+j*200,760,ease(bt,.6+j*.25,.4)*(1-ease(bt,2.0,.3)),{size:24,pad:40})); }
    else { txt(c,'Now',W/2,250,{size:34,w:600,al:'center',col:PAL.accent,a:ease(bt,2.4,.4)}); kt(c,'TV. Radio. Satellite. Aircraft. Navigation.',W/2,770,bt,2.8,{size:44,w:500,al:'center',stagger:.1}); }
    return; }
  if(bt<13){ let i=0; while(i<11&&bt>=MSLOT[i+1][0]) i++; const [a,b]=MSLOT[i]; montFrame(c,i,bt);
    const kin=eio3(seg(bt,a,a+.18)); c.save(); c.translate((1-kin)*-40,0); label(c,MONT[i][0],MONT[i][1],90,H-120,kin); c.restore();
    txt(c,String(i+1).padStart(2,'0')+' / 12',W-90,H-70,{size:22,w:600,mono:true,al:'right',col:'#c9d3dc'});
    const f=seg(bt,a,a+.12); if(f<1){ c.fillStyle=`rgba(255,255,255,${.18*(1-f)})`; c.fillRect(0,0,W,H); }
    return; }
  // 13..16: the build, everything flickers past faster and faster
  const rate=lerp(8,22,seg(bt,13,16)); const i=Math.floor(bt*rate)%12; montFrame(c,i,bt,{...MONT[i][3],z:MONT[i][3].z*lerp(1,1.25,seg(bt,13,16))});
  c.fillStyle=`rgba(5,7,10,${.25+.3*seg(bt,13,16)})`; c.fillRect(0,0,W,H);
  kt(c,'14 modes.',W/2,H/2+40,bt,13.2,{size:150,w:700,al:'center'});
};
// ---------------------------------------------------------------- 5. the wall: everything at once, cards fly in from depth
function wallTile(c,i,x,y,w,h,bt){ const [nm,sub,clip,cm0]=MONT[i]; const t=4+bt*.8+i*.3; const sr=[cm0.cx-W/(2*cm0.z)*1.1,cm0.cy-H/(2*cm0.z)*1.1,W/cm0.z*1.1,H/cm0.z*1.1];
  card(c,c3(clip,t),sr,x,y,w,h,{after:clip==='dvbt_tv'?(cc=>{ const s=Math.max(w/sr[2],h/sr[3]); const ox=x+(w-sr[2]*s)/2-sr[0]*s, oy=y+(h-sr[3]*s)/2-sr[1]*s; progInto(cc,t+30,[ox+TV3[0]*s,oy+TV3[1]*s,TV3[2]*s,TV3[3]*s]); }):null});
  txt(c,nm,x+16,y+h-16,{size:22,w:600}); }
T.wall=(c,bt)=>{
  bgd(c);
  const cols=4, w=410, h=231, gx=26, gy=26, x0=(W-(cols*w+(cols-1)*gx))/2, y0=150;
  for(let i=0;i<12;i++){ const k=eoBack(clamp((bt-i*.05)/.5)); if(k<=0) continue; const x=x0+(i%cols)*(w+gx), y=y0+Math.floor(i/cols)*(h+gy);
    c.save(); c.globalAlpha*=clamp(k); const s=lerp(1.5,1,clamp(k)); c.translate(x+w/2,y+h/2); c.scale(s,s); c.rotate((1-clamp(k))*(i%2?.06:-.06)); c.translate(-(x+w/2),-(y+h/2)); wallTile(c,i,x,y,w,h,bt); c.restore(); }
  kt(c,'One app. Every signal.',W/2,1000,bt,1.0,{size:56,w:600,al:'center',acc:[2,3]});
};
// ---------------------------------------------------------------- 6. spotlights: analog TV, aircraft, satellites
const SPOT=[['atv_video',{cx:370,cy:250,z:2.6},{cx:390,cy:260,z:2.3},'Analog TV is back.','PAL, SECAM and NTSC, in colour.'],['adsb_air',{cx:700,cy:250,z:2.0},{cx:740,cy:270,z:1.8},'Aircraft overhead.','ADS-B, live on a map.'],['gnss_sky',{cx:620,cy:240,z:2.2},{cx:640,cy:250,z:2.0},'Satellites in the sky.','GPS, GLONASS and BeiDou.']];
T.spot=(c,bt)=>{ const i=Math.min(2,Math.floor(bt/(10/3))), a=i*10/3; const [clip,z0,z1,h1,h2]=SPOT[i];
  const cm=camMix(z0,z1,eio3(seg(bt,a,a+10/3))); foot(c,c3(clip,3+(bt-a)),cm);
  const f=seg(bt,a,a+.12); if(f<1){ c.fillStyle=`rgba(255,255,255,${.2*(1-f)})`; c.fillRect(0,0,W,H); }
  caption(c,bt-a,.2,h1,h2);
};
// ---------------------------------------------------------------- 7. native radios: 0.1.0 needed plug-ins, 0.2.0 has its own drivers
const NATIVE=['HackRF One / Pro','RTL-SDR','Airspy','BladeRF','LimeSDR','PlutoSDR','USRP'];
T.radios=(c,bt)=>{
  bgd(c);
  kt(c,'Native radios.',120,150,bt,.1,{size:66,w:600});
  // then
  const kL=ease(bt,.3,.5); c.save(); c.globalAlpha*=kL*.85; panel(c,120,250,700,640,{fill:'#0b0e12'}); txt(c,'0.1.0',150,310,{size:28,w:600,col:C.dim});
  txt(c,'HackRF',150,400,{size:36,w:600,col:'#9aa4ae'}); txt(c,'native',450,400,{size:24,w:400,col:'#6f7984'});
  ['Every other radio:','install SoapySDR,','then a plug-in','for each one.'].forEach((s,j)=>txt(c,s,150,480+j*54,{size:30,w:400,col:'#7a848f'})); c.restore();
  // now
  const kR=eoBack(clamp((bt-.2)/.5)); c.save(); c.globalAlpha*=clamp(kR); c.translate((1-clamp(kR))*80,0); panel(c,880,250,920,640,{active:true,fill:'#0c1218'}); txt(c,'0.2.0',910,310,{size:28,w:600,col:PAL.accent});
  NATIVE.forEach((s,j)=>{ const on=ease(bt,.8+j*.45,.35); const y=380+j*70; c.save(); c.globalAlpha*=.35+.65*on; check(c,930,y-10,16,on>.5?PAL.ok:'#4a5059'); txt(c,s,965,y,{size:34,w:600}); txt(c,j===0?'own driver':'built in',1760,y,{size:22,w:500,al:'right',col:on>.5?PAL.ok:C.dim}); c.restore(); });
  txt(c,'Nothing else to install.',910,860,{size:28,w:500,col:'#c2ccd6',a:ease(bt,4.2,.5)}); c.restore();
};
// ---------------------------------------------------------------- 8. faster: a real measurement, 0.1.4 against 0.2.0
T.speed=(c,bt)=>{
  bgd(c);
  kt(c,'And much faster.',120,170,bt,.1,{size:70,w:600,acc:[2]});
  txt(c,'CPU time to decode a DVB-T channel, same signal, same machine',120,250,{size:24,w:400,col:C.dim,a:ease(bt,.6,.5)});
  const bars=[['0.1.4',310,'#7a848f'],['0.2.0',97,PAL.accent]], x0=120, maxw=1200;
  bars.forEach(([v,pc,col],j)=>{ const k=eio3(seg(bt,1.0+j*.8,2.2+j*.8)); const y=330+j*170; txt(c,v,x0,y+40,{size:34,w:600,col:j?C.ink:C.dim,a:ease(bt,.8+j*.8,.4)});
    rr(c,x0+150,y,Math.max(8,maxw*(pc/320)*k),70,8); c.fillStyle=col; c.fill(); txt(c,Math.round(pc*k)+'% of real time',x0+170+maxw*(pc/320)*k,y+48,{size:30,w:600,a:k}); });
  const gk=ease(bt,3.2,.4); if(gk>0){ c.save(); c.globalAlpha*=gk; const s_=1+.2*(1-gk); c.translate(1500,760); c.scale(s_,s_); txt(c,'3.2× less',0,0,{size:96,w:700,al:'right',col:'#fff'}); c.restore(); }
  ['The screen redraws only when something changes.','Every receiver re-tuned for speed.'].forEach((s,j)=>kt(c,s,120,830+j*56,bt,4.5+j*.5,{size:30,w:400,col:'#c2ccd6',stagger:.03}));
  txt(c,'Measured with OnAir’s own load meter (dect2cli, DVB-T 8K 64-QAM, 30 s).',120,1050,{size:17,w:400,col:'#5f6872',a:ease(bt,1,.5)});
};
// ---------------------------------------------------------------- 9. and more
const MORE=[['Updates itself.','Checks GitHub, installs on quit.','dvbt_lock',[1700,0,220,60]],['A test signal for every mode.','Try everything, no radio needed.','atv_video',[150,80,560,330]],['Click to tune.','Right on the spectrum or waterfall.','dvbt_hover',[900,500,700,280]],['On every screen.','Network tuner, HLS and AirPlay.','dvbt_tv',[440,170,1000,580]]];
T.more=(c,bt)=>{
  bgd(c);
  kt(c,'And so much more.',W/2,130,bt,.1,{size:60,w:600,al:'center'});
  MORE.forEach(([h1,h2,clip,sr],j)=>{ const k=eoBack(clamp((bt-1-j*1.0)/.45)); if(k<=0) return; const w=820, h=330, x=W/2-w-20+(j%2)*(w+40), y=200+Math.floor(j/2)*(h+120);
    c.save(); c.globalAlpha*=clamp(k); const s=lerp(.85,1,clamp(k)); c.translate(x+w/2,y+h/2); c.scale(s,s); c.translate(-(x+w/2),-(y+h/2));
    const t=6+bt*.5; card(c,c3(clip,clip==='dvbt_hover'?3.2:t),sr,x,y,w,h,{after:clip==='dvbt_tv'?(cc=>{ const sc=Math.max(w/sr[2],h/sr[3]); const ox=x+(w-sr[2]*sc)/2-sr[0]*sc, oy=y+(h-sr[3]*sc)/2-sr[1]*sc; progInto(cc,t+40,[ox+TV3[0]*sc,oy+TV3[1]*sc,TV3[2]*sc,TV3[3]*sc]); }):null});
    txt(c,h1,x,y+h+44,{size:32,w:600}); txt(c,h2,x,y+h+78,{size:22,w:400,col:C.dim}); c.restore(); });
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
T.platforms=(c,bt)=>{
  c.fillStyle='#000'; c.fillRect(0,0,W,H); bgd(c);
  const tw=560, th=315, gap=48, x0=W/2-(3*tw+2*gap)/2, y0=330;
  const OS3=[['macOS','dvbt_lock',20],['Windows','atv_video',5],['Linux','adsb_air',5]];
  OS3.forEach(([os,clip,ct],i)=>{ const k=eoBack(clamp((bt-.2-i*.25)/.5)); if(k<=0) return; const fx=x0+i*(tw+gap), fy=y0;
    c.save(); c.globalAlpha*=clamp(k); c.translate(fx+tw/2,fy+th/2); c.scale(lerp(.8,1,clamp(k)),lerp(.8,1,clamp(k))); c.translate(-(fx+tw/2),-(fy+th/2));
    rr(c,fx,fy,tw,th,12); c.save(); c.clip(); desktop(c,os,bt,fx,fy,tw,th); const sc=tw/W; appWindow(c,os,c3(clip,ct+bt*.5),fx+300*sc,fy+206*sc,1320*sc,bt+5+i*3); c.restore();
    txt(c,os,fx+tw/2,fy+th+60,{size:36,w:600,al:'center'}); c.restore(); });
  kt(c,'Native on macOS, Windows and Linux.',W/2,220,bt,.4,{size:60,w:600,al:'center'});
};
// ---------------------------------------------------------------- 11. finale: all of it, at once
T.finale=(c,bt)=>{
  bgd(c);
  const cols=4, w=410, h=231, gx=26, gy=26, x0=(W-(cols*w+(cols-1)*gx))/2, y0=130; const dim=1-.6*ease(bt,1.6,.6);
  for(let i=0;i<12;i++){ const k=clamp((bt+.3-i*.02)/.3); if(k<=0) continue; const x=x0+(i%cols)*(w+gx), y=y0+Math.floor(i/cols)*(h+gy);
    c.save(); c.globalAlpha*=k*dim; const s=lerp(1.25,1,eio3(k)); c.translate(x+w/2,y+h/2); c.scale(s,s); c.translate(-(x+w/2),-(y+h/2)); wallTile(c,i,x,y,w,h,bt+6); c.restore(); }
  const k=ease(bt,1.6,.8); if(k>0){ c.save(); c.globalAlpha=k; const g=c.createLinearGradient(0,330,0,760); g.addColorStop(0,'rgba(5,7,10,0)'); g.addColorStop(.5,'rgba(5,7,10,.82)'); g.addColorStop(1,'rgba(5,7,10,0)'); c.fillStyle=g; c.fillRect(0,330,W,430); c.restore(); }
  kt(c,'All of it. At once.',W/2,560,bt,1.6,{size:116,w:700,al:'center',ls:-3});
  const ps=['DVB-T2','DVB-T','ATSC','ATSC 3.0','ISDB-T','DVB-S2','DTMB','Analog TV','DAB+','FM','DRM','ADS-B','GNSS','DMR'];
  let cx0=180; ps.forEach((s,i)=>{ c.font='400 21px Inter'; const ww=c.measureText(s).width+34; chip(c,s,cx0+ww/2,660,ease(bt,2.4+i*.08,.4),{size:21,pad:34}); cx0+=ww+10; });
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
  kt(c,'Free and open source. macOS, Windows and Linux.',W/2,ty+th+210,bt,2.5,{size:30,w:400,al:'center',col:'#c2ccd6',stagger:.04});
  const gk=ease(bt,6.0,.35); if(gk>0){ c.save(); c.globalAlpha=gk; const s_=1+.12*(1-gk); c.translate(W/2,ty+th+300); c.scale(s_,s_); txt(c,'github.com/adswill/OnAir',0,0,{size:58,w:600,ls:-.6,col:'#8cc8ff',al:'center'}); c.restore(); }
};
// ---------------------------------------------------------------- timeline (120 BPM: one bar = 2 s) and cue grid for music.py
const B=[['title',0,10,null,0],['look',10,20,'dissolve',.5],['newui',20,32,'cut',0],['modes',32,48,'dissolve',.4],['wall',48,54,'cut',0],['spot',54,64,'dissolve',.4],['radios',64,76,'cut',0],['speed',76,84,'dissolve',.4],['more',84,94,'cut',0],['platforms',94,100,'dissolve',.4],['finale',100,108,'cut',0],['outro',108,120,'dissolve',.6]];
// hits: 4 the version, 20 the new UI, 48 the wall, 64 native radios, 84 and more, 100 all of it, 114 the link
const HITS_T=[4,20,48,64,84,100,114];
const BUILDS=[[0,4],[16,20],[44,48],[60,64],[80,84],[92,100]];
const CUES=[5.5,6.5,7.0,7.5,8.0, 10.5,12.0, 22.0,24.0,26.5, 32.5,34.0,34.5, 36.0,37.0,38.0,39.0,40.0,41.0,42.0,42.5,43.0,43.5,44.0,44.5, 49.0, 54.5,57.5,61.0, 65.0,65.5,66.0,66.5,67.0,67.5,68.0, 77.0,78.0,79.0, 85.0,86.0,87.0,88.0, 94.5,95.0,95.5, 102.0, 108.5,109.5,110.5];
const SCENES=B.map((s,i)=>{ const next=B[i+1]; const t0=s[1]-(s[4]||0)/2; const t1=s[2]+(next?next[4]/2:0); const o=[s[0],t1-t0,s[3],s[4]||0]; o.t0=t0; o.t1=t1; o.start=s[1]; o.idx=i; return o; });
const TOTAL=120; window.TOTAL=TOTAL; window.NF=Math.round(TOTAL*FPS);
window.SCENES=SCENES.map(s=>({name:s[0],t0:s.t0,t1:s.t1,tr:s[2],trd:s[3]}));
const bufA=mk(), bufB=mk(), ca=bufA.getContext('2d'), cb=bufB.getContext('2d');
function sceneAt(c,s,t){ c.clearRect(0,0,W,H); const bt=t-s.start; CURBT=bt; CURT=t; c.save(); T[s[0]](c,bt); c.restore(); }
const MB=new Set(['newui','modes','spot']);   // camera moves get a short shutter (3 sub-frames)
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
window.HOVERX=[-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 831, -1, -1, -1, 843, 855, 867, 867, 879, 891, 891, 903, 915, 927, 927, 1016, 1028, 951, 1028, 1040, 1052, 1052, 1064, 999, -1, 1011, 1023, -1, -1, 1047, -1, -1, 1071, -1, -1, 1095, 1107, -1, 1119, -1, -1, 1143, 1143, -1, 1167, 1167, -1, -1, 1191, 1203, 1203, 1215, -1, -1, 1239, 1239, 1251, 1263, 1275, 1275, 1287, 1299, 1299, 1299, 1311, 1323, 1335, 1335, 1347, 1359, 1359, 1371, 1383, 1395, 1395, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, 1407, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1];
window.renderFrame=async(i)=>{ const t=i/FPS; NEED=new Set(); composite(t); const need=NEED; NEED=null; if(need.size){ await ensure(need); composite(t); } post(t,i); return t; };
