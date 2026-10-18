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
  if(LOADED.size>170){ const ks=[...LOADED.keys()]; for(let k=0;k<LOADED.size-120;k++) LOADED.delete(ks[k]); } }
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
  const y=top?170:H-170, x=o.x??110;
  c.translate(0,(1-ease(bt,t0,1.0))*10);
  txt(c,l1,x,y,{size:o.size??64,w:700,ls:-(o.size??64)*.03}); if(l2) txt(c,l2,x+2,y+(o.size??64)*.3+42,{size:29,w:300,col:'#c2ccd6',a:ease(bt,t0+.25,1.0)});
  c.restore(); }
// ---- callout: a dot on the footage, a hairline and a small label
function callout(c,bt,t0,cm,fx,fy,label,dx,dy){ const k=ease(bt,t0,.8); if(k<=0) return; const [x,y]=toScreen(cm,fx,fy); const lx=x+dx, ly=y+dy;
  c.save(); c.globalAlpha*=k; c.strokeStyle='rgba(150,205,240,.9)'; c.lineWidth=1.6; c.beginPath(); c.moveTo(x,y); c.lineTo(lerp(x,lx,k),lerp(y,ly,k)); c.stroke();
  c.fillStyle='#9fd3f5'; c.beginPath(); c.arc(x,y,5,0,7); c.fill(); c.strokeStyle='rgba(159,211,245,.35)'; c.lineWidth=2; c.beginPath(); c.arc(x,y,11+4*Math.sin(bt*3),0,7); c.stroke();
  c.font='500 30px Inter'; const tw=c.measureText(label).width+40; const bx=dx<0?lx-tw:lx, by=ly-27; rr(c,bx,by,tw,54,10); c.fillStyle='rgba(8,12,17,.96)'; c.fill(); c.strokeStyle='rgba(150,205,240,.55)'; c.lineWidth=1.2; c.stroke();
  txt(c,label,bx+20,by+37,{size:30,w:500}); c.restore(); }
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

// ================================================================ v5: release 0.2.0, cut to the piano score's hits. Real footage of 0.2.0 (and of 0.1.x for the before/after).
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
// a dark band under text that sits on footage, so it never fights the busy UI behind it
function scrim(c,k,top=false){ if(k<=0) return; c.save(); c.globalAlpha*=k; const y0=top?0:H-340, g=c.createLinearGradient(0,y0,0,y0+340);
  const a=[[0,'rgba(5,7,10,0)'],[.55,'rgba(5,7,10,.78)'],[1,'rgba(5,7,10,.94)']]; (top?a.slice().reverse().map(([o,col])=>[1-o,col]):a).forEach(([o,col])=>g.addColorStop(o,col)); c.fillStyle=g; c.fillRect(0,y0,W,340); c.restore(); }

const LOCK_OFF=4.4;
// ---------------------------------------------------------------- 3. the new UI: a guided camera tour of the live app in 3D
// Each stop: the camera whips to one part of the real UI (frame coordinates), the rest dims, the part gets a glowing frame and a statement.
const TEXF=mk();
const TOUR=[  // t, camera {cx,cy: frame point; z: zoom; tx,ty: where that point sits on screen (from centre); ry,rx: tilt}, region, label
  [0,  {cx:960,cy:540,z:.8,tx:0,ty:40,ry:.24,rx:.07}],
  [1.4,{cx:960,cy:540,z:.9,tx:0,ty:10,ry:.06,rx:.02}],
  [2.0,{cx:765,cy:95,z:1.85,tx:160,ty:-170,ry:-.12,rx:.04},[300,70,1230,118],['Found.','Locked.','Playing.'],'Mode, guard interval and code rate, detected on their own.','L'],
  [4.0,{cx:148,cy:270,z:2.0,tx:-420,ty:0,ry:.16,rx:.02},[5,118,293,422],['Every mode,','one click away.'],'Fourteen modes in one list: TV, radio and data.','R'],
  [6.0,{cx:870,cy:928,z:1.4,tx:-40,ty:-330,ry:-.1,rx:-.05},[312,838,1432,1018],['See the','signal itself.'],'Constellations, SNR, MER and error correction, live.','L'],
  [8.0,{cx:940,cy:464,z:1.25,tx:330,ty:0,ry:-.14,rx:.02},[456,194,1424,734],['Watch it','right here.'],'Services, guide and picture in one window.','L'],
  [10.0,{cx:1000,cy:645,z:1.45,tx:-170,ty:60,ry:.1,rx:.03},[338,500,1558,792],['Click the','waterfall.'],'Anywhere on the spectrum or the waterfall.','R'],
  [12.35,{cx:178,cy:37,z:3.1,tx:-260,ty:-40,ry:.12,rx:.02},[58,14,296,60],['Tuned.'],'600.000 to 607.926 MHz, in one click.','R']];
function tourCam(bt){ let i=0; while(i<TOUR.length-1&&bt>=TOUR[i+1][0]) i++; const s=TOUR[i], prev=TOUR[Math.max(0,i-1)];
  const wd=i<=1?1.4:.42, k=i===0?0:eio3(seg(bt,s[0],s[0]+wd)); const a=i===1?TOUR[0][1]:prev[1], b=s[1]; const m={};
  for(const key in b) m[key]=lerp(a[key],b[key],k); const hold=seg(bt,s[0]+wd,(TOUR[i+1]||[14])[0]); m.z*=1+.035*hold; return {cam:m,i,k,hold}; }
function camScreen(m){ return {pos:[m.tx-(m.cx-W/2)*m.z, m.ty-(m.cy-H/2)*m.z, 0], w:W*m.z, h:H*m.z, rx:m.rx, ry:m.ry}; }
function fp(o,x,y){ const q=rot3([(x/W-.5)*o.w,(y/H-.5)*o.h,0],o.rx,o.ry); return proj([q[0]+o.pos[0],q[1]+o.pos[1],q[2]+o.pos[2]]); }   // frame point -> screen
T.newui=(c,bt)=>{
  cine(c,bt+20,{glow:.14}); const {cam,i,k,hold}=tourCam(bt), st=TOUR[i];
  const seg_=bt<8.22?0:bt<10.22?1:2, t=seg_===0?LOCK_OFF+bt:seg_===1?4+(bt-8):1.2+(bt-10), clip=['dvbt_lock','dvbt_tv','dvbt_click'][seg_];
  tex(TEXF,x=>{ const im=img(c3(clip,t)); if(im) x.drawImage(im,0,0,W,H); if(clip==='dvbt_tv') prog3(x,t+20); else if(clip==='dvbt_lock'&&t>=12) prog3(x,t,false); });
  const o=camScreen(cam); screen3(c,TEXF,{...o,nx:28,ny:16,glow:i<=1?.6:.25,sheen:i<=1?seg(bt,.2,1.8):null});
  // spotlight on the stop's region
  if(st[2]){ const [x0,y0,x1,y1]=st[2], sp=eio3(seg(bt,st[0]+.15,st[0]+.55))*(1-eio3(seg(bt,(TOUR[i+1]||[99])[0]-.12,(TOUR[i+1]||[99])[0])));
    if(sp>0){ const q=[fp(o,x0-8,y0-8),fp(o,x1+8,y0-8),fp(o,x1+8,y1+8),fp(o,x0-8,y1+8)];
      c.save(); c.globalAlpha*=sp; c.beginPath(); c.rect(0,0,W,H); q.forEach((p,j)=>j?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.closePath(); c.fillStyle='rgba(3,5,8,.72)'; c.fill('evenodd');
      c.shadowColor='rgba(92,168,219,.9)'; c.shadowBlur=30; c.strokeStyle='rgba(150,210,250,.95)'; c.lineWidth=2.5; c.beginPath(); q.forEach((p,j)=>j?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.closePath(); c.stroke();
      c.shadowBlur=0; c.lineWidth=5; c.strokeStyle='#fff'; q.forEach((p,j)=>{ const a=q[(j+3)%4], b=q[(j+1)%4], L=28; const da=Math.hypot(a[0]-p[0],a[1]-p[1]), db=Math.hypot(b[0]-p[0],b[1]-p[1]); c.beginPath(); c.moveTo(p[0]+(a[0]-p[0])/da*L,p[1]+(a[1]-p[1])/da*L); c.lineTo(p[0],p[1]); c.lineTo(p[0]+(b[0]-p[0])/db*L,p[1]+(b[1]-p[1])/db*L); c.stroke(); });
      c.restore(); }
    // the statement, on the side away from the region
    const L=st[3], left=st[5]==='L', x=left?110:W-110, al=left?'left':'right', out=(TOUR[i+1]||[99])[0]-.15;
    const gs=c.createLinearGradient(left?0:W,0,left?W*.55:W*.45,0); gs.addColorStop(0,`rgba(3,5,8,${.75*sp})`); gs.addColorStop(1,'rgba(3,5,8,0)'); c.fillStyle=gs; c.fillRect(0,0,W,H);
    const y0_=H/2-(L.length-1)*62+40;
    L.forEach((l,j)=>slam(c,[[l,j===L.length-1&&L.length>1?1:0]],x,y0_+j*124,bt,st[0]+.2+j*.14,{size:L.length>2?104:116,al,from:1.35,t1:out}));
    txt(c,st[4],x,y0_+L.length*124-30,{size:30,w:400,al,col:'#c8d2dc',a:seg(bt,st[0]+.6,st[0]+.9)*(1-seg(bt,out,out+.2))}); }
  // the cursor and the click (frame 96 of the click clip = bt 12.0, at frame point 1180,640)
  if(seg_===2){ const ct=bt-10, mx=lerp(760,1180,eio3(seg(ct,.3,1.7))), my=lerp(700,640,eio3(seg(ct,.3,1.7))), [sx,sy]=fp(o,mx,my);
    const r=seg(ct,2.0,2.6); if(r>0&&r<1){ c.save(); c.strokeStyle=`rgba(160,215,250,${1-r})`; c.lineWidth=3; for(const d of [0,.25]){ const rr_=clamp(r-d)*90; if(rr_>0){ c.beginPath(); c.arc(sx,sy,rr_,0,7); c.stroke(); } } c.restore(); }
    if(ct<2.5) cursor(c,sx,sy,ct>1.95&&ct<2.1?1:0); }
  if(i<=1) slam(c,[['Meet the new ',0],['OnAir.',1]],W/2,H-90,bt,.5,{size:104,t1:1.8});
  const f=i>=2?seg(bt,st[0],st[0]+.12):1; if(f<1){ c.fillStyle=`rgba(200,230,255,${.12*(1-f)})`; c.fillRect(0,0,W,H); }
};
// ---------------------------------------------------------------- 4. from 3 standards to 14 modes, then the montage
const MONT=[['DVB-T2 / DVB-T','Europe, Asia, Africa','dvbt_tv',{cx:940,cy:470,z:1.55}],['ATSC 3.0','NextGen TV','atsc3_tv',{cx:900,cy:430,z:1.5}],['ISDB-T','Japan, Brazil','isdbt_tv',{cx:900,cy:430,z:1.5}],
  ['DVB-S / S2','Satellite','dvbs',{cx:950,cy:420,z:1.3}],['DTMB','China, Hong Kong','dtmb',{cx:950,cy:420,z:1.3}],['Analog TV','PAL, SECAM, NTSC','atv_video',{cx:700,cy:400,z:1.6}],
  ['DAB / DAB+','Digital radio','dab_radio',{cx:950,cy:420,z:1.3}],['FM','Stereo and RDS','fm',{cx:950,cy:420,z:1.3}],['DRM','Shortwave to VHF','drm_radio',{cx:900,cy:420,z:1.4}],
  ['ADS-B','Aircraft, 1090 MHz','adsb_air',{cx:900,cy:420,z:1.4}],['GNSS','GPS, GLONASS, BeiDou','gnss_sky',{cx:900,cy:420,z:1.4}],['DMR','Digital voice','dmr',{cx:950,cy:420,z:1.3}]];
const MSLOT=(()=>{ const s=[]; let t=4; for(let i=0;i<12;i++){ const d=i<4?1.0:.5; s.push([t,t+d]); t+=d; } return s; })();   // 4..12 s
function montFrame(c,i,bt,cmOverride){ const [nm,sub,clip,cm0]=MONT[i]; const t=3+bt*.8+i*.3; const cm=cmOverride||{...cm0,z:cm0.z*(1+.03*(bt%1))};
  foot(c,c3(clip,t),cm,cc=>{ if(clip==='dvbt_tv') prog3(cc,t+30); }); }
// ---------------------------------------------------------------- 5. the wall: everything at once, cards fly in from depth
function wallTile(c,i,x,y,w,h,bt){ const [nm,sub,clip,cm0]=MONT[i]; const t=4+bt*.8+i*.3; const sr=[cm0.cx-W/(2*cm0.z)*1.1,cm0.cy-H/(2*cm0.z)*1.1,W/cm0.z*1.1,H/cm0.z*1.1];
  card(c,c3(clip,t),sr,x,y,w,h,{after:clip==='dvbt_tv'?(cc=>{ const s=Math.max(w/sr[2],h/sr[3]); const ox=x+(w-sr[2]*s)/2-sr[0]*s, oy=y+(h-sr[3]*s)/2-sr[1]*s; progInto(cc,t+30,[ox+TV3[0]*s,oy+TV3[1]*s,TV3[2]*s,TV3[3]*s]); }):null});
  txt(c,nm,x+16,y+h-16,{size:22,w:600}); }
// ---------------------------------------------------------------- 6. spotlights: analog TV, aircraft, satellites
const SPOT=[['atv_video',{cx:370,cy:250,z:2.6},{cx:390,cy:260,z:2.3},'Analog TV is back.','PAL, SECAM and NTSC, in colour.'],['adsb_air',{cx:700,cy:250,z:2.0},{cx:740,cy:270,z:1.8},'Aircraft overhead.','ADS-B, live on a map.'],['gnss_sky',{cx:520,cy:690,z:2.0},{cx:470,cy:720,z:2.2},'Satellites, tracked.','GPS, GLONASS and BeiDou.']];
const SPS=[0,3.5,7,10];
T.spot=(c,bt)=>{ const i=bt<SPS[1]?0:bt<SPS[2]?1:2, a=SPS[i], b=SPS[i+1]; const [clip,z0,z1,h1,h2]=SPOT[i];
  const cm=camMix(z0,z1,eio3(seg(bt,a,b))); foot(c,c3(clip,(clip==='gnss_sky'?6:3)+(bt-a)),cm);
  const f=seg(bt,a,a+.12); if(f<1){ c.fillStyle=`rgba(255,255,255,${.2*(1-f)})`; c.fillRect(0,0,W,H); }
  caption(c,bt-a,.2,h1,h2,{top:clip==='gnss_sky',size:100});
};
// ---------------------------------------------------------------- 7. native radios: the old way during the build, the new list slams in on the hit (bt 4)
const NATIVE=['HackRF One / Pro','RTL-SDR','Airspy','BladeRF','LimeSDR','PlutoSDR','USRP'];
T.radios=(c,bt)=>{
  cine(c,bt+64);
  kt(c,'Radios in 0.1.x',120,170,bt,.1,{size:96,w:700,t1:3.6,col:'#9aa4ae'});
  slam(c,[['Native ',1],['radios.',0]],120,170,bt,4.0,{size:112,al:'left',glow:true});
  // then: centred and big during the build, pushed aside and dimmed on the hit
  const mv=eio3(seg(bt,3.9,4.3)), sc=lerp(1,.72,mv), cx=lerp(W/2,90+380,mv), cy=lerp(590,600,mv);
  c.save(); c.globalAlpha*=ease(bt,.2,.5)*lerp(1,.55,mv); c.translate(cx,cy); c.scale(sc,sc); c.translate(-W/2,-590);
  panel(c,W/2-520,300,1040,580,{fill:'#0b0e12'});
  txt(c,'HackRF',W/2-460,400,{size:46,w:600,col:'#c2ccd6'}); txt(c,'built in',W/2+460,400,{size:30,w:500,al:'right',col:'#7a848f'});
  txt(c,'Every other radio:',W/2-460,500,{size:40,w:500,col:'#9aa4ae',a:ease(bt,.8,.4)});
  ['1.  Install SoapySDR.','2.  Find the plug-in for your radio.','3.  Install that too.'].forEach((t_,j)=>{ const k=ease(bt,1.4+j*.6,.4); txt(c,t_,W/2-460+(1-k)*-20,580+j*80,{size:40,w:400,col:'#7a848f',a:k}); });
  c.restore();
  // now
  const kR=eoBack(clamp((bt-3.85)/.4)); if(kR>0){ c.save(); c.globalAlpha*=clamp(kR); c.translate((1-clamp(kR))*160,0); panel(c,880,250,940,700,{active:true,fill:'#0c1218'}); txt(c,'0.2.0',920,320,{size:30,w:600,col:PAL.accent});
    NATIVE.forEach((s_,j)=>{ const on=ease(bt,4.3+j*.5,.3); const y=400+j*72; c.save(); c.globalAlpha*=.3+.7*on; c.translate((1-on)*12,0); check(c,940,y-12,18,on>.5?PAL.ok:'#4a5059'); txt(c,s_,980,y,{size:40,w:600}); txt(c,j===0?'own driver':'built in',1780,y,{size:26,w:500,al:'right',col:on>.5?PAL.ok:C.dim}); c.restore(); });
    txt(c,'Nothing else to install.',920,915,{size:34,w:600,col:'#e3e9ef',a:ease(bt,8.0,.5)}); c.restore(); }
};
// ---------------------------------------------------------------- 8. faster: measured receiver CPU (dect2cli load meter, same recording, same machine)
// DVB-T 8K 64-QAM file: 0.1.0 305-307 %, 0.2.0 102-104 %. ISDB-T test recording: 0.1.5 172 %, 0.2.0 138-141 %. (% of real time = CPU cores busy.)
const SPEEDG=[['DVB-T',[['0.1.0',3.06],['0.2.0',1.03]],'3× less',.8],['ISDB-T',[['0.1.5',1.72],['0.2.0',1.40]],'19% less',3.0]];
T.speed=(c,bt)=>{
  cine(c,bt+76,{glow:.1});
  slam(c,[['Less CPU. ',1],['Same signal.',0]],120,170,bt,.2,{size:96,al:'left'});
  txt(c,'Receiver load in CPU cores, decoding the same recording on the same machine',122,232,{size:26,w:400,col:C.dim,a:ease(bt,.5,.5)});
  const bx=330, maxw=860, per=maxw/3.2;
  SPEEDG.forEach(([mode,rows,gain,t0],g)=>{ const y0=330+g*290;
    txt(c,mode,120,y0+20,{size:44,w:700,a:ease(bt,t0-.2,.4)});
    rows.forEach(([v,cores],j)=>{ const y=y0+60+j*84, k=eio3(seg(bt,t0+j*.6,t0+j*.6+.9)), now=v==='0.2.0';
      txt(c,v,120,y+44,{size:32,w:600,col:now?PAL.accent:'#8d97a1',mono:true,a:ease(bt,t0+j*.6-.1,.3)});
      if(k>0){ rr(c,bx,y+4,Math.max(10,cores*per*k),56,8); c.fillStyle=now?PAL.accent:'#5d6670'; c.fill();
        txt(c,(cores*k).toFixed(1)+((cores*k).toFixed(1)==='1.0'?' core':' cores'),bx+cores*per*k+22,y+44,{size:32,w:600,col:now?'#fff':'#aeb8c2',a:k}); } });
    const gk=ease(bt,t0+1.6,.35); if(gk>0){ c.save(); c.globalAlpha*=gk; const s_=1+.25*(1-gk); c.translate(W-110,y0+150); c.scale(s_,s_); c.shadowColor='rgba(92,168,219,.6)'; c.shadowBlur=40*gk; txt(c,gain,0,0,{size:124,w:800,al:'right',col:'#fff',ls:-4}); c.restore(); } });
  kt(c,'Every receiver re-tuned. The screen redraws only when something changes.',120,960,bt,5.0,{size:34,w:500,col:'#d3dbe3',stagger:.03});
  txt(c,'Measured with OnAir’s own load meter (dect2cli): DVB-T 8K 64-QAM recording, ISDB-T 13-segment test recording, 30 s each, two runs.',120,1040,{size:18,w:400,col:'#5f6872',a:ease(bt,1,.5)});
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
// ================================================================ v6: cinematic pass. App screens placed in 3D (perspective, depth of field,
// glow), full-screen slam type on the beats, and more of the live app on screen. Timeline and music unchanged.
const F3=2200;   // focal length (px): world z=0 maps 1:1 onto the frame
function proj(p){ const s=F3/(F3+p[2]); return [W/2+p[0]*s, H/2+p[1]*s, s]; }
function rot3(p,rx,ry,rz=0){ let [x,y,z]=p, c_=Math.cos(ry), s_=Math.sin(ry); [x,z]=[x*c_+z*s_,-x*s_+z*c_]; c_=Math.cos(rx); s_=Math.sin(rx); [y,z]=[y*c_-z*s_,y*s_+z*c_];
  if(rz){ c_=Math.cos(rz); s_=Math.sin(rz); [x,y]=[x*c_-y*s_,x*s_+y*c_]; } return [x,y,z]; }
// a flat screen in space: src (image or canvas) on a w x h rectangle centred at pos, turned by rx/ry/rz; drawn as a grid of affine tiles
function screen3(c,src,o){ const pos=o.pos||[0,0,0], w=o.w||1600, h=o.h||w*9/16, rx=o.rx||0, ry=o.ry||0, rz=o.rz||0, nx=o.nx||24, ny=o.ny||14;
  const P=(u,v)=>{ const q=rot3([(u-.5)*w,(v-.5)*h,0],rx,ry,rz); return proj([q[0]+pos[0],q[1]+pos[1],q[2]+pos[2]]); };
  const cs=[P(0,0),P(1,0),P(1,1),P(0,1)]; const poly=()=>{ c.beginPath(); cs.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.closePath(); };
  c.save(); c.globalAlpha*=(o.a??1);
  c.save(); c.shadowColor=o.glow?`rgba(92,168,219,${.5*o.glow})`:'rgba(0,0,0,.7)'; c.shadowBlur=o.glow?110:80; c.shadowOffsetY=o.glow?0:34*cs[0][2]; poly(); c.fillStyle='#0a0b0d'; c.fill(); c.restore();
  if(src){ const sw=src.naturalWidth||src.width, sh=src.naturalHeight||src.height, dw=sw/nx, dh=sh/ny; const m=c.getTransform();
    c.save(); poly(); c.clip();
    for(let j=0;j<ny;j++) for(let i=0;i<nx;i++){ const A=P(i/nx,j/ny), Bq=P((i+1)/nx,j/ny), Cq=P(i/nx,(j+1)/ny);
      c.setTransform(m.a,m.b,m.c,m.d,m.e,m.f); c.transform((Bq[0]-A[0])/dw,(Bq[1]-A[1])/dw,(Cq[0]-A[0])/dh,(Cq[1]-A[1])/dh,A[0],A[1]);
      const ow=Math.min(dw*1.04,sw-i*dw), oh=Math.min(dh*1.04,sh-j*dh); c.drawImage(src,i*dw,j*dh,ow,oh,0,0,ow,oh); }
    c.setTransform(m); c.restore(); }
  // light: the side turned away darkens, a soft sheen follows the angle
  const sh_=Math.min(.55,Math.abs(Math.sin(ry))*.9+Math.abs(Math.sin(rx))*.4); if(sh_>.01){ const g=c.createLinearGradient(cs[0][0],0,cs[1][0],0); const dark=`rgba(0,0,0,${sh_})`, clear='rgba(0,0,0,0)';
    g.addColorStop(0,ry>0?clear:dark); g.addColorStop(1,ry>0?dark:clear); poly(); c.fillStyle=g; c.fill(); }
  if(o.sheen!=null){ const sx=lerp(cs[0][0],cs[1][0],o.sheen); c.save(); poly(); c.clip(); c.globalCompositeOperation='lighter'; const g=c.createLinearGradient(sx-260,0,sx+260,0); g.addColorStop(0,'rgba(255,255,255,0)'); g.addColorStop(.5,'rgba(190,225,255,.16)'); g.addColorStop(1,'rgba(255,255,255,0)'); c.fillStyle=g; c.fillRect(0,0,W,H); c.restore(); }
  poly(); c.strokeStyle=o.glow?`rgba(140,200,240,${.3+.4*o.glow})`:'rgba(255,255,255,.16)'; c.lineWidth=1.4; c.stroke();
  c.restore(); return cs; }
// depth of field: anything drawn by fn is blurred as one layer
const SCR=mk(), scrx=SCR.getContext('2d');
function blurred(c,px,fn){ if(px<.4){ fn(c); return; } scrx.setTransform(1,0,0,1,0,0); scrx.clearRect(0,0,W,H); fn(scrx); c.save(); c.filter=`blur(${px.toFixed(1)}px)`; c.drawImage(SCR,0,0); c.restore(); }
// textures: footage frames with the programme drawn in, rendered into small canvases
const TEXL=[0,1,2,3].map(()=>mk(1920,1080)), TEXS=[...Array(14)].map(()=>mk(960,540));
function tex(buf,fn){ const x=buf.getContext('2d'); x.setTransform(1,0,0,1,0,0); x.fillStyle='#0b0c0e'; x.fillRect(0,0,buf.width,buf.height); x.save(); x.scale(buf.width/W,buf.height/H); fn(x); x.restore(); return buf; }
function texClip(buf,clip,t,cm){ return tex(buf,x=>{ if(cm) foot(x,c3(clip,t),cm,cc=>{ if(clip==='dvbt_tv') prog3(cc,t+30); }); else { const im=img(c3(clip,t)); if(im) x.drawImage(im,0,0,W,H); if(clip==='dvbt_tv') prog3(x,t+30); } }); }
// background: near-black with a slow accent glow and out-of-focus specks
function cine(c,bt,o={}){ bgd(c); const gx=W/2+Math.sin(bt*.2)*200, gy=o.gy??H*.62; const g=c.createRadialGradient(gx,gy,0,gx,gy,W*.62); g.addColorStop(0,`rgba(60,120,170,${o.glow??.16})`); g.addColorStop(1,'rgba(60,120,170,0)'); c.fillStyle=g; c.fillRect(0,0,W,H);
  c.save(); c.globalCompositeOperation='lighter'; for(let i=0;i<26;i++){ const r=18+hash(i*7.1)*70, x=((hash(i*3.3)*W+bt*(8+hash(i)*22)*(i%2?1:-1))%(W+200)+W+200)%(W+200)-100, y=hash(i*5.7)*H+Math.sin(bt*.3+i)*20;
    const gg=c.createRadialGradient(x,y,0,x,y,r); gg.addColorStop(0,`rgba(120,180,230,${.035+.04*hash(i*9.1)})`); gg.addColorStop(1,'rgba(120,180,230,0)'); c.fillStyle=gg; c.fillRect(x-r,y-r,2*r,2*r); } c.restore(); }
// slam type: arrives big and blurred, snaps into place on the beat
function slam(c,s,x,y,bt,t0,o={}){ const d=o.d??.26, k=seg(bt,t0-d*.6,t0+d*.4); if(k<=0) return; const out=o.t1!=null?seg(bt,o.t1,o.t1+.22):0; if(out>=1) return;
  const e=1-Math.pow(1-k,3), sc=lerp(o.from??1.5,1,e)*(1+.06*out); const size=o.size||150;
  c.save(); c.globalAlpha*=clamp(k*2.5)*(1-out); c.translate(x,y); c.scale(sc,sc); if(k<1||out>0) c.filter=`blur(${((1-e)*16+out*10).toFixed(1)}px)`;
  if(o.glow){ c.shadowColor='rgba(92,168,219,.6)'; c.shadowBlur=50; }
  if(Array.isArray(s)){ c.font=`${o.w||800} ${size}px Inter`; c.letterSpacing=(-size*.035)+'px'; const ws=s.map(p=>c.measureText(p[0]).width), tot=ws.reduce((a,b)=>a+b,0); let xx=o.al==='left'?0:o.al==='right'?-tot:-tot/2;
    s.forEach((p,i)=>{ txt(c,p[0],xx,0,{size,w:o.w||800,col:p[1]?PAL.accent:(o.col||'#fff'),ls:-size*.035}); xx+=ws[i]; }); }
  else txt(c,s,0,0,{size,w:o.w||800,al:o.al||'center',col:o.col||'#fff',ls:-size*.035});
  c.restore(); }
const lerp3=(a,b,k)=>a.map((v,i)=>lerp(v,b[i],k));

// ---------------------------------------------------------------- 1. title: odometer through 0.1.x, 0.2.0 slams in, the app rises behind, four statements
const ROLL=[[0,'0.1.0'],[1.0,'0.1.1'],[1.5,'0.1.2'],[2.0,'0.1.3'],[2.5,'0.1.4'],[3.0,'0.1.5'],[4.0,'0.2.0']];
T.title=(c,bt)=>{
  cine(c,bt,{glow:.06+.14*seg(bt,4,5)}); const out=1-ease(bt,9.85,.35); c.save(); c.globalAlpha*=out;
  if(bt>=4){ const k=eio3(seg(bt,4,6.5)); const t=bt-4;
    blurred(c,lerp(14,7,k),x=>screen3(x,texClip(TEXL[0],'dvbt_tv',2+t),{pos:[lerp(0,60,k),lerp(900,260,k),lerp(1600,700,k)],w:1700,rx:lerp(.7,.32,k),ry:lerp(-.2,-.12,k),a:.75*k,glow:.6,nx:16,ny:9})); }
  if(bt<4.6){ const sk=seg(bt,.3,3.6), fo=1-seg(bt,4,4.6);
    c.save(); c.globalAlpha*=.28*ease(bt,0,.8)*fo; c.filter='blur(3px)'; waterfall(c,0,0,W,H,bt+3,[{u:.5,w:.38,k:sk*sk}],.05); c.fillStyle='rgba(4,6,9,.72)'; c.fillRect(0,0,W,H); c.restore();
    c.save(); c.globalAlpha*=fo; spectrum(c,0,760,W,300,bt+3,[{u:.5,w:.38,k:sk*sk}],{n:600,lw:2.2,noFrame:true,grid:false,band:false}); c.restore();
    if(bt<1.2){ c.save(); c.globalAlpha=.35*(1-bt/1.2); c.globalCompositeOperation='lighter'; const ox=(hash(Math.floor(bt*30)*1.7)*256)|0; for(let x=-ox;x<W;x+=256) for(let y=-ox;y<H;y+=256) c.drawImage(grain,x,y); c.restore(); } }
  let i=0; while(i<ROLL.length-1&&bt>=ROLL[i+1][0]) i++; const v=ROLL[i][1], since=bt-ROLL[i][0];
  if(bt<4){ const yy=640, step=280, k=eio3(clamp(since/.24)), prev=i>0?ROLL[i-1][1]:null, shake=bt>3?Math.sin(bt*90)*4*seg(bt,3,4):0;
    c.save(); c.translate(shake,0); c.beginPath(); c.rect(0,yy-230,W,290); c.clip(); c.globalAlpha*=ease(bt,.2,.6);
    if(prev&&k<1) txt(c,prev,W/2,yy-k*step,{size:240,w:700,al:'center',col:'#5d6670',ls:-10});
    txt(c,v,W/2,yy+(1-k)*step,{size:240,w:700,al:'center',col:lerp(0,1,seg(bt,2.5,4))>.5?'#9aa4ae':'#6f7984',ls:-10}); c.restore();
    txt(c,'OnAir',W/2,330,{size:52,w:600,al:'center',col:'#7d8691',a:ease(bt,.3,.6)});
  } else { const m=eio3(seg(bt,5.6,6.2)), y=lerp(600,300,m), s_=lerp(1,.5,m);
    c.save(); c.translate(W/2,y); c.scale(s_,s_); slam(c,'0.2.0',0,0,bt,4.0,{size:330,w:800,glow:true,from:1.8}); c.restore();
    c.save(); c.globalAlpha*=1-m; c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=40; c.drawImage(logo,W/2-60,150,120,120); txt(c,'OnAir',W/2,330,{size:56,w:600,al:'center'}); c.restore();
    const S4=[[['New ',0],['interface.',1]],[['14 ',1],['modes.',0]],[['Native ',1],['radios.',0]],[['Less ',0],['CPU.',1]]];
    S4.forEach((s,j)=>slam(c,s,W/2,640,bt,6+j,{size:150,t1:j<3?6.62+j:null}));
  }
  c.restore(); };

// ---------------------------------------------------------------- 2. a new look: the 0.1.x screen swings away, 0.2.0 swings in, the camera flies into it
T.look=(c,bt)=>{
  cine(c,bt+10); const fin=eio3(seg(bt,6,10)), sw=eio3(seg(bt,2,3.2));
  const base={pos:[0,60,200],w:1500,ry:-.05*seg(bt,0,2)};
  if(sw<1){ screen3(c,img(c2('overview',6+bt*.2)),{pos:[lerp(0,-1100,sw),60,lerp(200,700,sw)],w:1500,ry:lerp(base.ry,-1.15,sw),a:1-sw*.8,nx:16,ny:9}); }
  if(sw>0){ const pos=lerp3(lerp3([1100,60,700],[0,60,200],sw),[0,0,0],fin); screen3(c,img(c3('dvbt_lock',4.4)),{pos,w:lerp(1500,W,fin),h:lerp(1500*9/16,H,fin),ry:lerp(lerp(1.15,0,sw),0,fin)+(.06*Math.sin(bt*.5))*(1-fin)*(sw>=1?1:0),glow:(1-fin)*(sw<1?1:.4),sheen:sw<1?sw:null,nx:28,ny:16}); }
  const pk=1-ease(bt,6,.5); if(pk>0){ c.save(); c.globalAlpha*=pk; pill(c,sw<.5?'0.1.x':'0.2.0',W/2,170+40,{accent:sw>=.5,size:30}); c.restore(); }
  slam(c,[['A whole ',0],['new look.',1]],W/2,130,bt,.4,{size:110,t1:5.8});
};
// ---------------------------------------------------------------- 4. modes: the count, then a 3D carousel of the live receivers
T.modes=(c,bt)=>{
  if(bt<4){ cine(c,bt);
    txt(c,'In 0.1.0',W/2,250,{size:46,w:500,al:'center',col:C.dim,a:ease(bt,.2,.5)*(1-ease(bt,2.15,.3))});
    txt(c,'Now',W/2,250,{size:46,w:700,al:'center',col:PAL.accent,a:ease(bt,2.6,.4)});
    const n=bt<2?3:Math.min(14,3+Math.floor((bt-2)/.05)), big=n===14, pulse=bt>=2&&!big?1+.04*((bt-2)/.05%1):1;
    c.save(); c.globalAlpha*=ease(bt,.2,.5); c.translate(W/2,690); c.scale(pulse,pulse); if(big){ c.shadowColor='rgba(92,168,219,.7)'; c.shadowBlur=80; } txt(c,String(n),0,0,{size:460,w:800,al:'center',col:big?'#fff':'#c8d0d8',ls:-20}); c.restore();
    txt(c,bt<2?'standards':'modes',W/2,800,{size:64,w:500,al:'center',col:big?PAL.accent:C.dim,a:ease(bt,.4,.5)});
    if(bt<2.3) ['DVB-T2','DVB-T','ATSC'].forEach((s,j)=>chip(c,s,W/2-260+j*260,900,ease(bt,.6+j*.25,.4)*(1-ease(bt,2.0,.3)),{size:34,pad:50}));
    else ['TV.','Radio.','Satellite.','Aircraft.','Navigation.'].forEach((s,j)=>slam(c,s,W/2-640+[0,190,410,690,980][j]+40,920,bt,2.7+j*.25,{size:56,w:700,al:'left',from:1.3}));
    return; }
  if(bt<12){ let i=0; while(i<11&&bt>=MSLOT[i+1][0]) i++; const [a,b]=MSLOT[i], p=seg(bt,a,b), [nm,sub,clip,cm0]=MONT[i];
    cine(c,bt,{glow:.12});
    c.save(); c.globalAlpha*=.10; txt(c,nm.toUpperCase(),W/2+lerp(260,-260,p),H/2+120,{size:300,w:900,al:'center',ls:-8}); c.restore();
    const t=3+bt*.8+i*.3; screen3(c,texClip(TEXL[0],clip,t,{...cm0,z:cm0.z*.92}),{pos:[lerp(140,-140,p),-30,lerp(80,-40,p)],w:1500,ry:lerp(.2,-.2,p),rx:.05,glow:.5,sheen:p,nx:18,ny:10});
    scrim(c,.85); slam(c,nm,110,H-110,bt,a+.02,{size:i<4?96:84,al:'left',from:1.25,d:.18}); txt(c,sub,114,H-56,{size:30,w:400,col:'#c2ccd6',a:seg(bt,a,a+.15)});
    txt(c,String(i+1).padStart(2,'0')+' / 12',W-90,H-70,{size:26,w:600,mono:true,al:'right',col:'#c9d3dc'});
    const f=seg(bt,a,a+.1); if(f<1){ c.fillStyle=`rgba(255,255,255,${.22*(1-f)})`; c.fillRect(0,0,W,H); }
    return; }
  const i=Math.floor((bt-12)*4)%12; montFrame(c,i,bt,{...MONT[i][3],z:MONT[i][3].z*lerp(1,1.2,seg(bt,12,14))});
  c.fillStyle=`rgba(5,7,10,${.5+.2*seg(bt,12,14)})`; c.fillRect(0,0,W,H);
  slam(c,[['14 ',1],['modes.',0]],W/2,H/2+70,bt,12.0,{size:230,glow:true});
};
// ---------------------------------------------------------------- 5. the wall: twelve live receivers on a plane in depth, the camera tracks across it
function wall3(c,bt,o){ const cols=4, tw=o.tw||600, th=tw*9/16, g=o.gap||46, ry=o.ry, rx=o.rx, off=o.off, items=[];
  for(let i=0;i<12;i++){ const gx=((i%cols)-(cols-1)/2)*(tw+g), gy=(Math.floor(i/cols)-1)*(th+g); const q=rot3([gx,gy,0],rx,ry); const fly=o.fly?o.fly(i):0;
    items.push({i,pos:[q[0]+off[0],q[1]+off[1],q[2]+off[2]+fly*900],fly}); }
  items.sort((a,b)=>b.pos[2]-a.pos[2]);
  const buckets=new Map(); for(const it of items){ const bl=Math.round(Math.min(10,Math.abs(it.pos[2]-(o.focus??off[2]))/120)); if(!buckets.has(bl)) buckets.set(bl,[]); buckets.get(bl).push(it); }
  [...buckets.keys()].sort((a,b)=>b-a).forEach(bl=>blurred(c,bl,x=>buckets.get(bl).forEach(it=>{ const [nm,sub,clip,cm0]=MONT[it.i]; const t=4+bt*.8+it.i*.3;
    const cs=screen3(x,texClip(TEXS[it.i],clip,t,{...cm0,z:cm0.z*.95}),{pos:it.pos,w:tw,h:th,rx,ry,a:(o.a??1)*(1-it.fly),nx:8,ny:5,glow:o.glowAll?.35:0});
    if(o.labels!==false){ x.save(); x.globalAlpha*=(1-it.fly)*(o.a??1); txt(x,nm,cs[3][0]+12,cs[3][1]-16,{size:Math.max(14,30*cs[3][2]),w:700}); x.restore(); } })));
}
T.wall=(c,bt)=>{
  cine(c,bt+48,{glow:.18}); const k=bt/6;
  wall3(c,bt,{ry:-.42,rx:.12,off:[lerp(380,-260,k),-20,lerp(900,620,k)],focus:lerp(900,620,k)-60,fly:i=>1-eio3(clamp((bt+.35-i*.03)/.55))});
  scrim(c,1); slam(c,[['One app. ',0],['Every signal.',1]],W/2,H-90,bt,.6,{size:110});
};
// ---------------------------------------------------------------- 9. more: a live screen in 3D on the right, big statements on the left
const MORE3=[['dvbt_hover',3,null,['Click','to tune.'],'Right on the spectrum or the waterfall.'],['atv_video',3,null,['A test signal','for every mode.'],'Try everything, no radio needed.'],
  ['dvbt_tv',6,null,['On every','screen.'],'Network tuner, HLS and AirPlay.'],['dvbt_lock',22,{cx:1660,cy:150,z:2.6},['Updates','itself.'],'Checks GitHub, installs on quit.']];
T.more=(c,bt)=>{
  cine(c,bt+84,{gy:H*.5}); const i=clamp(Math.floor(bt/2.5),0,3), a=i*2.5, p=seg(bt,a,a+2.5); const [clip,t0,cm,lines,sub]=MORE3[i];
  const inK=eio3(seg(bt,a-.3,a+.3)), outK=i<3?eio3(seg(bt,a+2.2,a+2.5)):0;
  const t=t0+(bt-a)*(clip==='dvbt_hover'?.9:1);
  screen3(c,texClip(TEXL[0],clip,t,cm),{pos:[lerp(1400,360,inK)+lerp(0,-1500,outK),20,lerp(300,120,p)],w:1350,ry:lerp(-.9,-.32,inK)+lerp(0,.6,outK)+.05*p,rx:.04,glow:.55,sheen:p,a:1-outK*.7,nx:20,ny:12});
  if(clip==='dvbt_hover'){ /* the live tooltip moves in the footage itself */ }
  txt(c,'AND MORE',110,300,{size:26,w:700,col:PAL.accent,ls:6,a:1-outK});
  lines.forEach((l,j)=>slam(c,l,110,420+j*130,bt,a+.1+j*.12,{size:118,al:'left',from:1.3,t1:i<3?a+2.25:null}));
  txt(c,sub,114,420+lines.length*130+10,{size:32,w:400,col:'#c2ccd6',a:seg(bt,a+.5,a+.8)*(1-outK)});
};
// ---------------------------------------------------------------- 10. platforms: three desktops in an arc, the camera pushes in
const OSB=[0,1,2].map(()=>mk(1280,720));
T.platforms=(c,bt)=>{
  cine(c,bt+94); const push=eio3(seg(bt,4,8)), orb=Math.sin(bt*.4)*.06;
  const OS3=[['macOS','dvbt_lock',20,-1],['Windows','atv_video',5,0],['Linux','adsb_air',5,1]];
  OS3.forEach(([os,clip,ct,side],i)=>{ const k=eoBack(clamp((bt-.15-i*.2)/.5)); if(k<=0) return;
    const b=tex(OSB[i],x=>{ desktop(x,os,bt,0,0,W,H); appWindow(x,os,c3(clip,ct+bt*.5),300,206,1320,bt+5+i*3); });
    const cs=screen3(c,b,{pos:[side*700,lerp(60,30,push),lerp(side?360:0,side?300:-80,push)+(1-clamp(k))*600],w:660,ry:-side*.5+orb,rx:.04,a:clamp(k),nx:14,ny:8,glow:side?0:.4});
    txt(c,os,(cs[2][0]+cs[3][0])/2,cs[3][1]+70,{size:44,w:700,al:'center',a:clamp(k)}); });
  slam(c,[['Native on ',0],['macOS, Windows',1],[' and ',0],['Linux.',1]],W/2,220,bt,.4,{size:86});
};
// ---------------------------------------------------------------- 11. finale: the wall again, rushing at the camera, then the statement
T.finale=(c,bt)=>{
  cine(c,bt+102,{glow:.22}); const k=eio3(seg(bt,0,8)), dim=1-.55*ease(bt,1.6,.6);
  c.save(); c.globalAlpha*=dim; wall3(c,bt+6,{ry:lerp(.3,-.12,k),rx:lerp(.25,.06,k),off:[lerp(-200,120,k),0,lerp(1300,150,k)],focus:lerp(1300,150,k),labels:false,glowAll:true,fly:i=>1-clamp((bt+.3-i*.015)/.25)}); c.restore();
  const g0=ease(bt,1.6,.8); if(g0>0){ c.save(); c.globalAlpha=g0; const g=c.createLinearGradient(0,330,0,780); g.addColorStop(0,'rgba(5,7,10,0)'); g.addColorStop(.5,'rgba(5,7,10,.85)'); g.addColorStop(1,'rgba(5,7,10,0)'); c.fillStyle=g; c.fillRect(0,330,W,450); c.restore(); }
  slam(c,'All of it.',W/2-290,580,bt,1.6,{size:150}); slam(c,[['At once.',1]],W/2+320,580,bt,2.1,{size:150,glow:true});
  const ps=['DVB-T2','DVB-T','ATSC','ATSC 3.0','ISDB-T','DVB-S2','DTMB','Analog TV','DAB+','FM','DRM','ADS-B','GNSS','DMR'];
  let cx0=150; ps.forEach((s,i)=>{ c.font='500 24px Inter'; const ww=c.measureText(s).width+38; chip(c,s,cx0+ww/2,690,ease(bt,2.6+i*.08,.4),{size:24,pad:38,w:500}); cx0+=ww+10; });
};
// ---------------------------------------------------------------- 12. outro: the TV settles flat, the name, the link on the last hit
T.outro=(c,bt)=>{
  cine(c,bt+110,{gy:H*.35,glow:.2}); const k=eio3(seg(bt,0,2.2)), lift=14*ease(bt,5.6,.8);
  const tw=1120, th=630; screen3(c,img(progSrc(bt+14)),{pos:[0,-H/2+90+th/2-lift,lerp(260,0,k)],w:tw,h:th,rx:lerp(.22,0,k),ry:lerp(-.25,0,k),glow:.5,nx:16,ny:9,sheen:seg(bt,.2,2.4)});
  const ty=90-lift; const bb=ease(bt,1.0,.5); if(bb>0){ const tx=W/2-tw/2; c.save(); c.globalAlpha=bb; rr(c,tx+24,ty+24,300,46,6); c.fillStyle='rgba(8,12,18,.8)'; c.fill(); c.fillStyle='#4fb36a'; c.beginPath(); c.arc(tx+50,ty+47,6,0,7); c.fill(); txt(c,'LIVE · Channel One HD',tx+66,ty+55,{size:20,w:600}); c.restore(); }
  fadeUp(c,bt,.5,()=>{ c.drawImage(logo,W/2-430,ty+th+56,92,92); txt(c,'OnAir '+VERSION,W/2-318,ty+th+130,{size:82,w:700,ls:-2.5}); },{dur:.8});
  const pk=ease(bt,1.5,.6); if(pk>0){ c.save(); c.globalAlpha=pk; pill(c,'Out now',W/2+330,ty+th+104,{accent:true,size:34}); c.restore(); }
  kt(c,'Free and open source. macOS, Windows and Linux.',W/2,ty+th+210,bt,2.5,{size:30,w:400,al:'center',col:'#c2ccd6',stagger:.04});
  const bk=seg(bt,6,7.4); if(bk>0&&bk<1){ c.save(); c.globalCompositeOperation='lighter'; const r=lerp(80,1300,eio3(bk)); const g=c.createRadialGradient(W/2,ty+th+290,0,W/2,ty+th+290,r); g.addColorStop(0,`rgba(140,200,255,${.45*(1-bk)})`); g.addColorStop(1,'rgba(140,200,255,0)'); c.fillStyle=g; c.fillRect(0,0,W,H);
    c.strokeStyle=`rgba(170,220,255,${.6*(1-bk)})`; c.lineWidth=3; c.beginPath(); c.ellipse(W/2,ty+th+290,r*.9,r*.28,0,0,7); c.stroke(); c.restore(); }
  slam(c,'github.com/adswill/OnAir',W/2,ty+th+300,bt,6.0,{size:64,w:700,col:'#8cc8ff',from:1.6,glow:true});
  const gl=seg(bt,7.2,8.4); if(gl>0&&gl<1){ c.save(); c.globalCompositeOperation='lighter'; const gx=lerp(W/2-500,W/2+500,gl); const g=c.createLinearGradient(gx-120,0,gx+120,0); g.addColorStop(0,'rgba(255,255,255,0)'); g.addColorStop(.5,'rgba(255,255,255,.35)'); g.addColorStop(1,'rgba(255,255,255,0)'); c.fillStyle=g; c.fillRect(gx-120,ty+th+40,240,300); c.restore(); }
};
// ---------------------------------------------------------------- timeline (120 BPM: one bar = 2 s) and cue grid for music.py
const B=[['title',0,10,null,0],['look',10,20,'cut',0],['newui',20,34,'cut',0],['modes',34,48,'zoom',.5],['wall',48,54,'cut',0],['spot',54,64,'whip',.4],['radios',64,76,'zoom',.5],['speed',76,84,'whip',.4],['more',84,94,'cut',0],['platforms',94,102,'zoom',.5],['finale',102,110,'cut',0],['outro',110,122,'zoom',.6]];
// hits: 4 the version, 20 the new UI, 68 native radios, 84 and more, 102 all of it, 116 the link
const HITS_T=[4,20,68,84,102,116];
const BUILDS=[[0,4],[16,20],[64,68],[80,84],[94,102]];
const SCENES=B.map((s,i)=>{ const next=B[i+1]; const t0=s[1]-(s[4]||0)/2; const t1=s[2]+(next?next[4]/2:0); const o=[s[0],t1-t0,s[3],s[4]||0]; o.t0=t0; o.t1=t1; o.start=s[1]; o.idx=i; return o; });
const TOTAL=122; window.TOTAL=TOTAL; window.NF=Math.round(TOTAL*FPS);
window.SCENES=SCENES.map(s=>({name:s[0],t0:s.t0,t1:s.t1,tr:s[2],trd:s[3]}));
const bufA=mk(), bufB=mk(), ca=bufA.getContext('2d'), cb=bufB.getContext('2d');
function sceneAt(c,s,t){ c.clearRect(0,0,W,H); const bt=t-s.start; CURBT=bt; CURT=t; c.save(); T[s[0]](c,bt); c.restore(); }
const MB=new Set(['newui','spot']);   // camera moves get a short shutter (3 sub-frames)
const mbA=mk(), mbx=mbA.getContext('2d');
const SLOT={spot:bt=>bt<SPS[1]?0:bt<SPS[2]?1:2, newui:bt=>bt<8.22?0:bt<10.22?1:2};   // no blur across a hard cut inside a scene
function sceneMB(c,s,t){ const sl=SLOT[s[0]]; if(!MB.has(s[0])||(sl&&sl(t-s.start)!==sl(t-s.start-2/90))){ sceneAt(c,s,t); return; } sceneAt(c,s,t); mbx.clearRect(0,0,W,H);
  for(const [dt,al] of [[-1/90,1/2],[-2/90,1/3]]){ sceneAt(mbx,s,t+dt); c.save(); c.globalAlpha=al; c.drawImage(mbA,0,0); c.restore(); } }
function composite(t){
  const act=SCENES.filter(s=>t>=s.t0&&t<s.t1);
  if(act.length===1){ sceneMB(ctx,act[0],t); return; }
  const A=act[0],Bn=act[1], p=clamp((t-Bn.t0)/Bn[3]); sceneAt(ca,A,t); sceneAt(cb,Bn,t);
  const e=eio3(p), tr=Bn[2]; ctx.fillStyle='#000'; ctx.fillRect(0,0,W,H);
  const lay=(buf,sc,dx,a,bl)=>{ ctx.save(); ctx.globalAlpha=a; if(bl>.3) ctx.filter=`blur(${bl.toFixed(1)}px)`; ctx.translate(W/2+dx,H/2); ctx.scale(sc,sc); ctx.drawImage(buf,-W/2,-H/2); ctx.restore(); };
  if(tr==='zoom'){ lay(bufA,lerp(1,1.35,e),0,1-e*.9,e*18); lay(bufB,lerp(.78,1,e),0,clamp(e*1.6),(1-e)*16);
    ctx.save(); ctx.globalCompositeOperation='lighter'; ctx.globalAlpha=.18*Math.sin(Math.PI*e); ctx.fillStyle='#9cc6e6'; ctx.fillRect(0,0,W,H); ctx.restore(); }
  else if(tr==='whip'){ lay(bufA,1,-W*.9*e,1-e*.6,Math.sin(Math.PI*e)*26); lay(bufB,1,W*.9*(1-e),clamp(e*1.8),Math.sin(Math.PI*e)*26); }
  else { ctx.drawImage(bufA,0,0); ctx.save(); ctx.globalAlpha=e; ctx.drawImage(bufB,0,0); ctx.restore(); }
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
