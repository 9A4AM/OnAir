// OnAir promo v5: demonstrations drawn on a canvas in the app's own palette (single accent colour, navy waterfall ramp,
// accent-coloured constellations on near-black plots), with depth: glass cards, a 3D waterfall, particle constellations,
// bloom, dust, camera drift and motion-blurred transitions. Scene functions get bt = seconds since the scene's bar line.
// Needs common_head.js. Every on-screen cue sits on the 120 BPM beat grid, matching music.py.
Object.assign(C,{blue:'#5ca8db',green:'#66c285',amber:'#e6b347',red:'#e06157',ink:'#e0e5eb',dim:'#7f8c99',dim2:'#4d565f'});
const PAL={accent:'#5ca8db',ok:'#66c285',warn:'#e6b347',bad:'#e06157',grey:'#9eabba',plot:'#030405',border:'#383f47',panel:'#141619'};
const acc=a=>`rgba(92,168,219,${a})`;
let CURBT=0, CURT=0; const bp=bt=>Math.exp(-7*(((bt%.5)+.5)%.5));      // 1 on every beat, decays before the next
const pop=(bt,t0)=>0;                                                   // no bounce: restrained motion
const eo4=k=>1-Math.pow(1-clamp(k),4);

// ---------------------------------------------------------------- signal model
function floorDb(u,t){return -86+vnoise(u*90+t*9)*3+vnoise(u*23-t*2)*2}
function ofdm(u,t,c0,half,k=1){const e=sstep(c0-half-.012,c0-half+.012,u)*(1-sstep(c0+half-.012,c0+half+.012,u)); const rip=vnoise(u*140+t*14)*2.2+Math.sin(u*38+t*.7)*1.4; return -90+e*k*(43+rip);}
function specDb(u,t,sigs){let v=floorDb(u,t); for(const s of sigs) v=Math.max(v,ofdm(u,t,s.u,s.w,s.k??1)); return v;}
const STOPS=[[6,10,20],[16,38,70],[30,100,140],[110,190,205],[240,250,250]];
function ramp(v){v=clamp(v)*4; const i=Math.min(3,Math.floor(v)), f=v-i; return STOPS[i].map((a,k)=>a+(STOPS[i+1][k]-a)*f);}
const wf=mk(360,150), wfx=wf.getContext('2d'), wfd=wfx.createImageData(360,150);
function waterfall(c,x,y,w,h,t,sigs,rowDt=.06){
  const d=wfd.data;
  for(let r=0;r<150;r++){const tau=t-r*rowDt; for(let i=0;i<360;i++){const v=(specDb(i/360,tau,sigs)+100)/68; const [R,G,B]=ramp(v); const o=(r*360+i)*4; d[o]=R;d[o+1]=G;d[o+2]=B;d[o+3]=255;}}
  wfx.putImageData(wfd,0,0); c.save(); c.imageSmoothingEnabled=true; c.drawImage(wf,x,y,w,h); c.restore();
}
function plotFrame(c,x,y,w,h){ c.fillStyle=PAL.plot; c.fillRect(x,y,w,h); c.strokeStyle=PAL.border; c.lineWidth=1.5; c.strokeRect(x+.75,y+.75,w-1.5,h-1.5); }
function spectrum(c,x,y,w,h,t,sigs,o={}){
  if(!o.noFrame) plotFrame(c,x,y,w,h); c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip();
  if(o.grid!==false){c.strokeStyle='rgba(153,163,179,.16)'; c.lineWidth=1; for(let i=1;i<10;i++){c.beginPath();c.moveTo(x+i*w/10,y);c.lineTo(x+i*w/10,y+h);c.stroke()} for(let i=1;i<6;i++){c.beginPath();c.moveTo(x,y+i*h/6);c.lineTo(x+w,y+i*h/6);c.stroke()}}
  const N=o.n||420, lo=-100, hi=-30, pts=[], pk=[];
  for(let i=0;i<=N;i++){const u=i/N, d=specDb(u,t,sigs); pts.push([x+u*w,y+h-clamp((d-lo)/(hi-lo))*h]); const p=specDb(u,t-.35,sigs)+3; pk.push([x+u*w,y+h-clamp((Math.max(d,p)-lo)/(hi-lo))*h]);}
  for(const s of sigs){ const k=s.k??1; if(k>0.02){c.fillStyle=acc(.10*Math.min(1,k)); c.fillRect(x+(s.u-s.w)*w,y,2*s.w*w,h);} }
  c.strokeStyle='rgba(158,171,186,.38)'; c.lineWidth=1.4; c.beginPath(); pk.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke();
  c.strokeStyle=PAL.accent; c.lineWidth=o.lw||2.2; c.lineJoin='round'; c.shadowColor=PAL.accent; c.shadowBlur=(o.glow??10)*.3; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke();
  c.restore();
  if(o.axes){ for(let i=0;i<=8;i++) txt(c,(598+i)+'',x+(i+.5)*w/9.0,y+h+26,{size:o.fs||15,mono:true,col:C.dim,al:'center'}); }
}
// 3D waterfall: stacked spectra, newest row at the front, hidden-line fill (the receiver's waterfall as terrain)
function ridge(c,x,y,w,h,t,sigs,o={}){
  const R=o.rows||32, N=o.n||160, dt=o.dt||.2, a=o.a??1;
  for(let r=0;r<R;r++){
    const d=r/(R-1), yy=y+h*(.30+.70*Math.pow(d,1.12)), sc=lerp(.58,1,d), xx=x+(1-sc)*w/2, ww=w*sc, amp=h*lerp(.30,.60,d), tau=t-(R-1-r)*dt, pts=[];
    for(let i=0;i<=N;i++){ const u=i/N, v=clamp((specDb(u,tau,sigs)+100)/68); pts.push([xx+u*ww,yy-v*amp]); }
    c.save(); c.globalAlpha=a; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.lineTo(xx+ww,yy+14); c.lineTo(xx,yy+14); c.closePath();
    c.fillStyle='rgb(5,7,10)'; c.fill();
    c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1]));
    c.lineJoin='round'; c.lineWidth=lerp(1.1,3,d); c.strokeStyle=d>.94?'rgba(205,232,248,.98)':`rgba(92,168,219,${lerp(.16,.95,Math.pow(d,1.4))})`;
    if(d>.8){ c.shadowColor=PAL.accent; c.shadowBlur=(18*d)*.3; } c.stroke(); c.restore(); }
}
function constel(c,x,y,s,M,sigma,t,o={}){
  plotFrame(c,x,y,s,s); const side=Math.sqrt(M), n=o.n||(M<=4?300:1400), cell=s/(side+.6); c.save(); c.beginPath(); c.rect(x+2,y+2,s-4,s-4); c.clip();
  const pos=i=>{ if(M===2) return [(i&1?1:-1)*.3*s,0]; if(M===4) return [(i&1?1:-1)*.2*s,((i>>1)&1?1:-1)*.2*s]; const a=i%side,b=Math.floor(i/side)%side; return [(a-(side-1)/2)*cell,(b-(side-1)/2)*cell]; };
  c.fillStyle=acc(o.a??.8); const ds=o.dot||Math.max(2,s/260);
  for(let k=0;k<n;k++){ const [px,py]=pos(Math.floor(hash(k*3.1+7)*M)); const ph=t*.6+k; const sg=sigma*cell;
    const gx=gauss(k)*sg+Math.sin(ph)*sg*.1, gy=gauss(k+9999)*sg+Math.cos(ph*1.3)*sg*.1; c.fillRect(x+s/2+px+gx-ds/2,y+s/2+py+gy-ds/2,ds,ds); }
  c.strokeStyle='rgba(255,255,255,.12)'; c.lineWidth=1; c.beginPath();
  if(M>4) for(let a=0;a<side;a++){const q=(a-(side-1)/2)*cell; c.moveTo(x+s/2+q,y+6);c.lineTo(x+s/2+q,y+s-6);c.moveTo(x+6,y+s/2+q);c.lineTo(x+s-6,y+s/2+q);}
  c.moveTo(x+s/2,y);c.lineTo(x+s/2,y+s);c.moveTo(x,y+s/2);c.lineTo(x+s,y+s/2);c.stroke(); c.restore();
}
function constelFX(c,x,y,s,M,k,t,o={}){ constel(c,x,y,s,M,lerp(.60,o.sigma??.045,k),t,{n:o.n,dot:o.dot}); }
function lamp(c,x,y,on,label,size=26){ c.fillStyle=on>0?PAL.ok:'#434a52'; c.beginPath(); c.arc(x,y,size*.3,0,7); c.fill(); if(on>0){c.save();c.globalAlpha=.5*on;c.shadowColor=PAL.ok;c.shadowBlur=(20)*.3;c.beginPath();c.arc(x,y,size*.3,0,7);c.fill();c.restore();} if(label) txt(c,label,x+size*.65,y+size*.35,{size,w:500,col:on>0?C.ink:C.dim}); }

// ---------------------------------------------------------------- depth: backdrop, glass, dust, type
function dust(c,a=1){}
function bgd(c,glow=.7){ c.fillStyle=C.bg; c.fillRect(0,0,W,H);
  const g=c.createRadialGradient(W*.7,H*.55,0,W*.7,H*.55,W*.75); g.addColorStop(0,`rgba(40,100,150,${.11*glow})`); g.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g; c.fillRect(0,0,W,H);
  const g2=c.createLinearGradient(0,0,0,H); g2.addColorStop(0,'rgba(255,255,255,.018)'); g2.addColorStop(1,'rgba(255,255,255,0)'); c.fillStyle=g2; c.fillRect(0,0,W,H); }
function glass(c,x,y,w,h,r=22,o={}){
  c.save(); c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=o.blur??46; c.shadowOffsetY=o.dy??20; rr(c,x,y,w,h,r);
  const g=c.createLinearGradient(0,y,0,y+h); g.addColorStop(0,o.top||'#161b21'); g.addColorStop(1,o.bot||'#0a0d10'); c.fillStyle=g; c.fill(); c.shadowColor='transparent';
  const s=c.createLinearGradient(0,y,0,y+h*.55); s.addColorStop(0,'rgba(255,255,255,.035)'); s.addColorStop(1,'rgba(255,255,255,0)'); rr(c,x,y,w,h,r); c.fillStyle=s; c.fill();
  const b=c.createLinearGradient(x,y,x+w*.7,y+h); b.addColorStop(0,o.active?'rgba(110,180,225,.75)':'rgba(255,255,255,.20)'); b.addColorStop(1,o.active?'rgba(92,168,219,.25)':'rgba(255,255,255,.05)');
  rr(c,x+.75,y+.75,w-1.5,h-1.5,r); c.strokeStyle=b; c.lineWidth=1.6; c.stroke();
  c.restore(); }
// per-letter kinetic type (rise + fade), returns the width; al: 'left' | 'center'
function kinetic(c,s,x,y,bt,t0,o={}){
  const size=o.size||64, w=o.w||600, ls=(o.ls??-size*.03), col=o.col||C.ink;
  const k=eo4(seg(bt,t0,t0+.8)); if(k<=0) return 0;
  c.save(); c.globalAlpha*=k; c.font=`${w} ${size}px Inter`; c.letterSpacing=ls+'px'; const tw=c.measureText(s).width; c.textAlign='left'; c.fillStyle=col;
  c.fillText(s,o.al==='center'?x-tw/2:x,y+(1-k)*size*.18); c.restore(); return tw; }
function sheen(c,s,x,y,bt,t0,o={}){ kinetic(c,s,x,y,bt,t0,{size:o.size||120,w:o.w||600,ls:o.ls,al:o.al,col:'#eef1f4'}); }
function head(c,l1,l2,t,t0,o={}){ const x=o.x??110,y=o.y??140,s=o.size??64; kinetic(c,l1,x,y,t,t0,{size:s,w:500,ls:-s*.035,stagger:.02,al:o.al||'left'}); if(l2) kinetic(c,l2,x,y+s*1.08,t,t0+.25,{size:s,w:500,ls:-s*.035,stagger:.02,col:o.col||PAL.accent,al:o.al||'left'}); }
function chip(c,s,cx,cy,a=1,o={}){ c.save(); c.globalAlpha*=a; const sc=1+.05*(o.pop||0); c.translate(cx,cy); c.scale(sc,sc); c.translate(-cx,-cy); c.font=`${o.w||500} ${o.size||24}px Inter`; const tw=c.measureText(s).width+(o.pad||44), th=(o.size||24)+26;
  rr(c,cx-tw/2,cy-th/2,tw,th,th/2); const g=c.createLinearGradient(0,cy-th/2,0,cy+th/2); g.addColorStop(0,o.fill||'rgba(255,255,255,.07)'); g.addColorStop(1,'rgba(255,255,255,.02)'); c.fillStyle=g; c.fill(); c.strokeStyle=o.stroke||'rgba(255,255,255,.2)'; c.lineWidth=1.4; c.stroke();
  txt(c,s,cx,cy+(o.size||24)*.35,{size:o.size||24,w:o.w||500,al:'center',col:o.col||C.ink}); c.restore(); return tw; }
const flash=(c,a)=>{};
function shock(){}
function burst(){}
function scan(c,a=.12){ c.save(); c.fillStyle=`rgba(0,0,0,${a})`; for(let y=0;y<H;y+=4) c.fillRect(0,y+2,W,2); c.restore(); }
function bez(a,b,c2,d,t){const u=1-t;return u*u*u*a+3*u*u*t*b+3*u*t*t*c2+t*t*t*d}
function wrapText(c,s,x,y,maxW,lh,col){ const words=s.split(' '); let line='',yy=y; c.fillStyle=col; for(const w of words){ const t=line?line+' '+w:w; if(c.measureText(t).width>maxW&&line){ c.fillText(line,x,yy); line=w; yy+=lh; } else line=t; } c.fillText(line,x,yy); }
function persp(c,src,cx,cy,sc,angDeg,alpha=1){ const sw=src.width, sh=src.height, ang=angDeg*Math.PI/180, strip=3, f=2600; c.save(); c.globalAlpha*=alpha;
  for(let x=0;x<sw;x+=strip){ const lx=(x-sw/2)*sc, z=lx*Math.sin(ang), px=lx*Math.cos(ang), s=f/(f+z), h=sh*sc*s; c.drawImage(src,x,0,strip,sh,cx+px*s,cy-h/2,strip*sc*Math.cos(ang)*s+.6,h); } c.restore(); }

// ---- broadcast picture: a dusk city skyline over water with a news lower third (content, not app UI)
function skyline(c,x,base,w,maxH,seed,col,windows,t,near,u){ let px=x,i=0; while(px<x+w){ const bw=((near?30:22)+hash(seed*10+i)*(near?70:44))*u, bh=maxH*(.25+.75*hash(seed*7+i*1.3)); c.fillStyle=col; c.fillRect(px,base-bh,bw+1,bh);
  if(windows){ for(let wy=base-bh+12*u; wy<base-10*u; wy+=15*u) for(let wx=px+7*u; wx<px+bw-9*u; wx+=12*u){ if(hash(seed*100+i*13+Math.round(wx*.37)+Math.round(wy*.91))>.58){ const tw=.7+.3*Math.sin(t*.5+wx*.07+wy*.05); c.fillStyle=`rgba(255,214,150,${.5*tw})`; c.fillRect(wx,wy,5*u,7*u); } } }
  px+=bw+hash(seed*3+i)*6*u; i++; } }
function tvPicture(c,x,y,w,h,t,o={}){
  const u=h/1080; c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip();
  c.translate(x+w/2+Math.sin(t*.25)*w*.01,y+h/2); c.scale(1.04+.01*Math.sin(t*.2),1.04+.01*Math.sin(t*.2)); c.translate(-(x+w/2),-(y+h/2));
  const hz=y+h*.62; let g=c.createLinearGradient(0,y,0,hz); g.addColorStop(0,'#08122a'); g.addColorStop(.45,'#1f3459'); g.addColorStop(.82,'#a96a5c'); g.addColorStop(1,'#e8a06a'); c.fillStyle=g; c.fillRect(x-60,y-60,w+120,hz-y+60);
  const sx=x+w*.64, sy=hz-h*.015; g=c.createRadialGradient(sx,sy,0,sx,sy,h*.6); g.addColorStop(0,'rgba(255,205,150,.85)'); g.addColorStop(.22,'rgba(255,155,100,.34)'); g.addColorStop(1,'rgba(255,120,70,0)'); c.fillStyle=g; c.fillRect(x-60,y-60,w+120,h+120);
  for(let i=0;i<6;i++){ const cy=y+h*(.10+.085*i), cx=x+(((hash(i*7.7)*1.5-.25)*w+t*(5+i*1.6))%(w*1.5)), cw=w*(.22+.18*hash(i*2.3)); const gg=c.createRadialGradient(0,0,0,0,0,cw); gg.addColorStop(0,`rgba(255,196,160,${.15-.011*i})`); gg.addColorStop(1,'rgba(255,196,160,0)'); c.save(); c.translate(cx,cy); c.scale(1,.07); c.fillStyle=gg; c.fillRect(-cw,-cw,cw*2,cw*2); c.restore(); }
  skyline(c,x-60,hz+1,w+120,h*.17,7,'rgba(74,66,96,.9)',false,t,false,u); skyline(c,x-60,hz+1,w+120,h*.27,11,'#101626',true,t,true,u);
  g=c.createLinearGradient(0,hz,0,y+h); g.addColorStop(0,'#2b2f46'); g.addColorStop(1,'#04060b'); c.fillStyle=g; c.fillRect(x-60,hz,w+120,y+h-hz+60);
  for(let i=0;i<46;i++){ const yy=hz+i*(y+h-hz)/46, wd=(10+i*3.2)*u*(.6+.4*Math.sin(t*1.8+i*1.7)); c.fillStyle=`rgba(255,178,112,${.24*(1-i/46)})`; c.fillRect(sx-wd/2+Math.sin(t*1.1+i)*5*u,yy,wd,(y+h-hz)/46*.5); }
  c.restore();
  // overlays: vignette, LIVE tag, channel bug, lower third
  c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip(); const vg=c.createRadialGradient(x+w/2,y+h/2,h*.4,x+w/2,y+h/2,h*.95); vg.addColorStop(0,'rgba(0,0,0,0)'); vg.addColorStop(1,'rgba(0,0,0,.38)'); c.fillStyle=vg; c.fillRect(x,y,w,h);
  c.fillStyle='#d9473c'; c.beginPath(); c.arc(x+w*.045,y+h*.075,h*.012,0,7); c.fill(); txt(c,'LIVE',x+w*.045+h*.026,y+h*.075+h*.012,{size:h*.03,w:600,col:'rgba(255,255,255,.9)',ls:2});
  txt(c,'ONE HD',x+w*.955,y+h*.075+h*.012,{size:h*.032,w:700,al:'right',col:'rgba(255,255,255,.75)',ls:3});
  const lt=eo4(seg(t,o.lt??.6,(o.lt??.6)+.9)); if(!o.noLower){ c.save(); c.translate(-(1-lt)*w*.4,0); c.globalAlpha=lt;
    const bx=x+w*.05, by=y+h*.78, bw=w*.44, bh=h*.14; let lg=c.createLinearGradient(bx,0,bx+bw,0); lg.addColorStop(0,'rgba(8,12,20,.9)'); lg.addColorStop(1,'rgba(8,12,20,.55)'); c.fillStyle=lg; c.fillRect(bx,by,bw,bh); c.fillStyle='#e8ebef'; c.fillRect(bx,by,h*.008,bh);
    txt(c,'Evening News',bx+h*.03,by+bh*.5,{size:h*.05,w:600}); txt(c,'Live  ·  18:00',bx+h*.03,by+bh*.84,{size:h*.03,w:400,col:'#b6c0cc'}); c.restore(); }
  c.restore();
}

const T={};
// ---------------------------------------------------------------- hook: Tune. Lock. Watch.
T.hook=(c,bt)=>{
  c.fillStyle='#000'; c.fillRect(0,0,W,H);
  const punch=t0=>1.045-.045*eo5(seg(bt,t0,t0+.6));
  if(bt<2.0){ const s=punch(0); c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
    const k=eo3(seg(bt,.15,1.5)); const g0=c.createRadialGradient(W/2,H*.55,0,W/2,H*.55,W*.6); g0.addColorStop(0,'rgba(40,110,170,.18)'); g0.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g0; c.fillRect(0,0,W,H);
    ridge(c,60,70,1800,850,bt+8,[{u:.5,w:.3,k}],{rows:34,n:170,dt:.2});
    for(let i=0;i<=8;i++) txt(c,(598+i)+'',W*(.12+i*.095),H-96,{size:20,mono:true,col:C.dim,al:'center',a:.8});
    c.restore();
    const g=c.createLinearGradient(0,H*.55,0,H); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.72)'); c.fillStyle=g; c.fillRect(0,H*.55,W,H*.45);
    const f=lerp(597.6,602.0,eo3(seg(bt,.1,1.5))); txt(c,f.toFixed(3)+' MHz',1840,96,{size:56,w:500,mono:true,al:'right'});
    kinetic(c,'Tune.',100,1010,bt,.02,{size:210,w:700,ls:-9,stagger:.06}); }
  else if(bt<4.0){ const tt=bt-2, s=punch(2); c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
    c.fillStyle='#050607'; c.fillRect(0,0,W,H); const g0=c.createRadialGradient(1390,540,0,1390,540,700); g0.addColorStop(0,'rgba(40,110,170,.2)'); g0.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g0; c.fillRect(0,0,W,H);
    constelFX(c,1000,150,780,64,eo3(seg(tt,.2,1.65)),bt,{n:2400,dot:4.2}); c.restore();
    kinetic(c,'Lock.',100,1010,bt,2.02,{size:210,w:700,ls:-9,stagger:.06});
    const mer=lerp(8.2,31.4,eo3(seg(tt,.3,1.6))); txt(c,'MER',110,170,{size:26,w:500,col:C.dim}); txt(c,mer.toFixed(1)+' dB',110,236,{size:64,w:500,mono:true,col:PAL.accent});
    ['IQ','P1','L1-pre','LDPC'].forEach((n,i)=>lamp(c,128,330+i*58,eo3(seg(bt,2.5+i*.25,2.75+i*.25)),n,30)); }
  else { const tt=bt-4, s=punch(4)*(1.0); c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
    tvPicture(c,0,0,W,H,bt,{noLower:true}); c.restore();
    const vg=c.createRadialGradient(W/2,H/2,H*.35,W/2,H/2,H*.95); vg.addColorStop(0,'rgba(0,0,0,0)'); vg.addColorStop(1,'rgba(0,0,0,.6)'); c.fillStyle=vg; c.fillRect(0,0,W,H);
    const g=c.createLinearGradient(0,H*.5,0,H); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.82)'); c.fillStyle=g; c.fillRect(0,H*.5,W,H*.5);
    kinetic(c,'Watch.',100,1010,bt,4.02,{size:210,w:700,ls:-9,stagger:.06}); }
  flash(c,.5*(1-seg(bt,0,.12))); flash(c,(bt>=2?.45:0)*(1-seg(bt,2,2.14))); flash(c,(bt>=4?.45:0)*(1-seg(bt,4,4.14)));
  shock(c,W/2,H/2,2.0,bt); shock(c,W/2,H/2,4.0,bt);
};
T.logo=(c,bt)=>{
  bgd(c,seg(bt,0,1));
  c.save(); c.globalAlpha=.7*eo3(seg(bt,.5,2)); ridge(c,0,690,W,420,bt+3,[{u:.5,w:.3,k:1}],{rows:18,n:140,dt:.25,a:.5}); c.restore();
  const lk=eo4(seg(bt,.1,.9))*(1+.03*bp(bt)*seg(bt,1,1.5)); c.save(); c.translate(W/2,330); c.scale(lk,lk);
  const gl=c.createRadialGradient(0,0,0,0,0,420); gl.addColorStop(0,'rgba(224,97,87,.13)'); gl.addColorStop(.5,'rgba(224,97,87,.04)'); gl.addColorStop(1,'rgba(224,97,87,0)'); c.fillStyle=gl; c.fillRect(-460,-460,920,920);
  c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=50; c.shadowOffsetY=20; c.globalAlpha=seg(bt,.1,.4); c.drawImage(logo,-170,-170,340,340); c.restore();
  burst(c,W/2,330,bt,.1,90); shock(c,W/2,330,.1,bt);
  sheen(c,'OnAir',W/2,700,bt,.9,{size:132,al:'center'});
  kinetic(c,'Digital TV, decoded in software.',W/2,780,bt,1.5,{size:46,w:400,ls:-1,stagger:.018,al:'center'});
  ['DVB-T2','DVB-T','ATSC'].forEach((s,i)=>chip(c,s,W/2+(i-1)*210,850,eo4(seg(bt,2.4+i*.5,3.0+i*.5)),{size:26,pop:pop(bt,2.5+i*.5)}));
};
// ---------------------------------------------------------------- any radio
const RADIOS=[['HackRF One / Pro','native driver','20',20],['Airspy','via SoapySDR','6–10',10],['SDRplay RSP','via SoapySDR','~10',10],['PlutoSDR','via SoapySDR','20+',20],['BladeRF','via SoapySDR','20+',20],['LimeSDR','via SoapySDR','20+',20],['USRP','via SoapySDR','20+',20],['RTL-SDR','narrow signals only','2.4',2.4]];
T.radios=(c,bt)=>{
  bgd(c,.9);
  head(c,'Bring your own radio.',null,bt,.2,{size:66,y:150});
  reveal(c,'HackRF natively. Everything else through SoapySDR.',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  const lx=110, ly=290, cw=520, ch=104, gx=24, gy=20, logoC=[1560,560];
  const lk=eo4(seg(bt,.6,1.6)); c.save(); c.translate(logoC[0],logoC[1]); c.scale(.96+.04*lk,.96+.04*lk); const gl=c.createRadialGradient(0,0,0,0,0,300); gl.addColorStop(0,'rgba(92,168,219,.14)'); gl.addColorStop(1,'rgba(92,168,219,0)'); c.fillStyle=gl; c.fillRect(-320,-320,640,640);
  c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=40; c.shadowOffsetY=16; c.globalAlpha=lk; c.drawImage(logo,-120,-120,240,240); c.restore();
  kinetic(c,'OnAir',logoC[0],logoC[1]+205,bt,1.2,{size:44,w:600,al:'center'});
  RADIOS.forEach((r,i)=>{ const col=i%2, row=Math.floor(i/2); const x=lx+col*(cw+gx), y=ly+row*(ch+gy); const a=eo4(seg(bt,1.0+i*.5,1.6+i*.5)); if(a<=0) return;
    const x1=x+cw, y1=y+ch/2; c.save(); c.globalAlpha=a*.32; c.strokeStyle=PAL.accent; c.lineWidth=1.4; c.beginPath(); c.moveTo(x1,y1); c.bezierCurveTo(x1+200,y1,logoC[0]-300,logoC[1],logoC[0]-150,logoC[1]); c.stroke(); c.restore();
    for(let p=0;p<3;p++){ const u=((bt*.3+p/3+i*.17)%1); const bx=bez(x1,x1+200,logoC[0]-300,logoC[0]-150,u), by=bez(y1,y1,logoC[1],logoC[1],u); c.save(); c.globalAlpha=a*Math.sin(u*Math.PI)*.8; c.fillStyle='#bcdcf0'; c.beginPath(); c.arc(bx,by,3,0,7); c.fill(); c.restore(); } });
  RADIOS.forEach((r,i)=>{ const col=i%2, row=Math.floor(i/2); const x=lx+col*(cw+gx), y=ly+row*(ch+gy); const a=eo4(seg(bt,1.0+i*.5,1.7+i*.5)); if(a<=0) return;
    c.save(); c.globalAlpha=a; c.translate((1-a)*-36,0); glass(c,x,y,cw,ch,16,{active:i===0,blur:26,dy:10});
    txt(c,r[0],x+28,y+46,{size:30,w:600,ls:-.4}); txt(c,r[1],x+28,y+80,{size:20,col:i===0?PAL.accent:C.dim,w:400});
    const bx=x+cw-215, bw=170, by=y+60, k=eo4(seg(bt,1.3+i*.5,2.1+i*.5)); txt(c,r[2]+' Msps',bx+bw,y+36,{size:19,mono:true,w:500,al:'right',col:C.ink});
    rr(c,bx,by,bw,8,4); c.fillStyle='#1b2026'; c.fill(); rr(c,bx,by,Math.max(8,bw*(r[3]/20)*k),8,4); c.fillStyle=i===7?PAL.warn:PAL.accent; c.fill();
    c.strokeStyle='rgba(255,255,255,.45)'; c.lineWidth=1.5; c.beginPath(); c.moveTo(bx+bw*.5,by-6); c.lineTo(bx+bw*.5,by+14); c.stroke(); c.restore(); });
  const lg=eo4(seg(bt,5.2,6)); c.save(); c.globalAlpha=lg; c.strokeStyle='rgba(255,255,255,.45)'; c.lineWidth=1.5; c.beginPath(); c.moveTo(114,1000); c.lineTo(114,1020); c.stroke(); txt(c,'Bars: maximum sample rate (scale to 20 Msps).   Marker: about 10 Msps, enough for an 8 MHz channel.',130,1016,{size:22,w:400,col:C.dim}); c.restore();
};
// ---------------------------------------------------------------- auto detection + scanner
T.auto=(c,bt)=>{
  bgd(c,.9);
  head(c,'It finds the signal.','You don’t configure a thing.',bt,.2,{size:60,y:140});
  const cards=[['DVB-T2','T2-Base and T2-Lite','1.7 – 10 MHz · up to 256-QAM',.34,'ofdm'],['DVB-T','2K and 8K','QPSK – 64-QAM · all guard intervals',.26,'ofdm'],['ATSC 1.0','8-VSB','6 MHz · decision-feedback equaliser',.30,'vsb']];
  const sel=bt<2.5?-1:Math.min(2,Math.floor((bt-2.5)/.5));
  cards.forEach((cd,i)=>{ const x=110+i*590, y=300, w=550, h=330, k=eo4(seg(bt,.7+i*.15,1.7+i*.15)); if(k<=0) return; const on=sel===i;
    c.save(); c.globalAlpha=clamp(k); c.translate(0,(1-clamp(k))*40); const pu=on?1+.012*bp(bt):1; c.translate(x+w/2,y+h/2); c.scale(pu,pu); c.translate(-(x+w/2),-(y+h/2));
    glass(c,x,y,w,h,22,{active:on}); kinetic(c,cd[0],x+30,y+66,bt,.9+i*.15,{size:48,w:600,ls:-1.5,stagger:.03}); txt(c,cd[1],x+30,y+102,{size:22,w:400}); txt(c,cd[2],x+30,y+132,{size:18,col:C.dim});
    const gx=x+30, gy=y+158, gw=w-60, gh=110; plotFrame(c,gx,gy,gw,gh); c.save(); c.beginPath(); c.rect(gx,gy,gw,gh); c.clip(); const pts=[];
    for(let j=0;j<=200;j++){ const u=j/200; const e=sstep(.5-cd[3]-.02,.5-cd[3]+.02,u)*(1-sstep(.5+cd[3]-.02,.5+cd[3]+.02,u)); let v=.1+e*(.52+vnoise(u*120+bt*12)*.05)+vnoise(u*50+bt*3)*.02; if(cd[4]==='vsb') v+=Math.exp(-Math.pow((u-(.5-cd[3]+.012))/.008,2))*.3; pts.push([gx+u*gw,gy+gh-clamp(v)*gh*1.2]); }
    c.strokeStyle=on?PAL.accent:'rgba(92,168,219,.5)'; c.lineWidth=2.2; if(on){c.shadowColor=PAL.accent;c.shadowBlur=(12)*.3;} c.beginPath(); pts.forEach((p,j)=>j?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke(); c.restore();
    const ok=on||(sel>i); txt(c,ok?'●  detected':'○  looking',x+30,y+h-18,{size:18,w:500,col:ok?PAL.ok:C.dim2,mono:true}); c.restore(); });
  const k=eo5(seg(bt,3.8,4.6)); if(k>0){ c.save(); c.globalAlpha=k; const x=110,y=740,w=1700,h=220; txt(c,'Channel scanner',x,y-64,{size:28,w:600,col:C.ink}); txt(c,'470 – 694 MHz',x+w,y-64,{size:20,mono:true,col:C.dim,al:'right'});
    const muxes=[[.12,'482'],[.38,'538'],[.62,'594'],[.86,'650']]; const sw=seg(bt,4.4,6.8);
    plotFrame(c,x,y,w,h); c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip(); const pts=[]; for(let i=0;i<=500;i++){ const u=i/500; let d=floorDb(u,bt); for(const m of muxes){ if(sw>m[0]-.01) d=Math.max(d,ofdm(u,bt,m[0],.035)); } pts.push([x+u*w,y+h-clamp((d+100)/68)*h]); }
    // radar-style sweep trail
    const tr=c.createLinearGradient(x+w*sw-w*.12,0,x+w*sw,0); tr.addColorStop(0,'rgba(92,168,219,0)'); tr.addColorStop(1,'rgba(92,168,219,.28)'); c.fillStyle=tr; c.fillRect(x+w*Math.max(0,sw-.12),y,w*Math.min(.12,sw),h);
    c.strokeStyle=PAL.accent; c.lineWidth=2.2; c.shadowColor=PAL.accent; c.shadowBlur=(10)*.3; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke(); c.shadowBlur=0;
    c.strokeStyle='rgba(220,240,255,.8)'; c.lineWidth=2; c.beginPath(); c.moveTo(x+w*sw,y); c.lineTo(x+w*sw,y+h); c.stroke(); c.restore();
    muxes.forEach(([u,l],i)=>{ const a=eo4(seg(bt,5+i*.5,5.6+i*.5)); if(a<=0) return; c.save(); c.globalAlpha=clamp(a); const px=x+u*w, pu=1+.08*pop(bt,5+i*.5); c.translate(px,y-14); c.scale(pu,pu); c.strokeStyle=PAL.ok; c.lineWidth=2; c.beginPath(); c.moveTo(0,14+h*.2); c.lineTo(0,6); c.stroke(); txt(c,l+' MHz',0,0,{size:20,w:600,mono:true,al:'center',col:PAL.ok}); c.restore(); });
    c.restore(); }
};
// ---------------------------------------------------------------- live TV
const tvBuf=mk(1140,690), tvx=tvBuf.getContext('2d');
T.live=(c,bt)=>{
  bgd(c,.9);
  head(c,'Watch live TV.',null,bt,.2,{size:66,y:150});
  reveal(c,'Video, audio, subtitles, teletext and a programme guide.',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  const k=eo5(seg(bt,.3,1.7)); const dolly=1+.012*clamp(bt,0,6);
  tvx.clearRect(0,0,1140,690); const x=40,y=40,w=1060,h=596; glass(tvx,x-18,y-18,w+36,h+36,34,{blur:50,dy:24,top:'#1a2026',bot:'#0c0f12'});
  tvx.save(); rr(tvx,x,y,w,h,18); tvx.clip(); tvPicture(tvx,x,y,w,h,bt,{lt:.9});
  const sb=eo5(seg(bt,2.5,3.1)); if(sb>0){ tvx.globalAlpha=sb; rr(tvx,x+w*.18,y+h*.64,w*.64,h*.075,8); tvx.fillStyle='rgba(0,0,0,.78)'; tvx.fill(); txt(tvx,'…and now, the evening news.',x+w*.5,y+h*.64+h*.052,{size:h*.04,w:500,al:'center',col:'#fff'}); }
  tvx.restore(); const gls=tvx.createLinearGradient(x,y,x+w*.6,y+h); gls.addColorStop(0,'rgba(255,255,255,.10)'); gls.addColorStop(.35,'rgba(255,255,255,0)'); tvx.fillStyle=gls; rr(tvx,x,y,w,h,18); tvx.fill();
  c.save(); c.translate(W/2,H/2); c.scale(dolly,dolly); c.translate(-W/2,-H/2);
  c.save(); c.globalAlpha=k; persp(c,tvBuf,110+570,290+345,1,lerp(-34,0,k));
  // reflection
  c.save(); c.translate(0,2*980); c.scale(1,-1); c.globalAlpha=.10*k; c.drawImage(tvBuf,110,290,1140,690); c.restore(); c.restore();
  c.save(); const fade=c.createLinearGradient(0,930,0,1080); fade.addColorStop(0,'rgba(5,6,7,0)'); fade.addColorStop(1,'rgba(5,6,7,1)'); c.fillStyle=fade; c.fillRect(0,930,W,150); c.restore();
  // teletext page
  const tk=eo4(seg(bt,1.2,2.2)); c.save(); c.globalAlpha=clamp(tk); c.translate((1-clamp(tk))*70,0);
  const tx=1280,ty=290,tw=530,th=596; glass(c,tx-14,ty-14,tw+28,th+28,26,{blur:40}); c.save(); rr(c,tx,ty,tw,th,12); c.clip(); c.fillStyle='#000'; c.fillRect(tx,ty,tw,th);
  const row=(n,s,col,bgc)=>{ const yy=ty+22+n*27; if(bgc){c.fillStyle=bgc;c.fillRect(tx+14,yy-21,tw-28,27);} txt(c,s,tx+22,yy,{size:21,mono:true,w:500,col}); };
  row(0,'P100  OnAir Text   18:04','#fff','#0000c8'); row(2,'NEWS','#ffeb3b'); row(3,'101 Headlines','#00e5ff'); row(4,'110 Politics','#00e5ff'); row(5,'120 World','#00e5ff'); row(7,'SPORT','#ffeb3b'); row(8,'200 Scores','#00e5ff'); row(9,'210 Fixtures','#00e5ff'); row(11,'WEATHER','#ffeb3b'); row(12,'300 Today','#00e5ff'); row(13,'310 Week ahead','#00e5ff');
  [['#e53935','NEWS'],['#43a047','SPORT'],['#fdd835','WEATHER'],['#00acc1','TV']].forEach(([cc,l],i)=>{ c.fillStyle=cc; c.fillRect(tx+14+i*(tw-28)/4,ty+th-60,(tw-28)/4-6,34); txt(c,l,tx+14+i*(tw-28)/4+((tw-28)/4-6)/2,ty+th-36,{size:17,mono:true,w:700,al:'center',col:'#000'}); });
  c.fillStyle='rgba(0,0,0,.18)'; for(let yy=0;yy<th;yy+=4) c.fillRect(tx,ty+yy+2,tw,2); c.restore(); c.restore();
  ['Live video + audio','Programme guide','DVB subtitles','Teletext'].forEach((s,i)=>chip(c,s,[310,720,1130,1480][i],985,eo4(seg(bt,1.5+i*.5,2.1+i*.5)),{size:24,pop:pop(bt,1.5+i*.5)}));
  c.restore();
  const q=eo5(seg(bt,3.4,4.2)); if(q>0){ c.save(); c.globalAlpha=q; txt(c,'Signal quality',1280,236,{size:20,col:C.dim}); rr(c,1280,246,530,16,8); c.fillStyle='#14181c'; c.fill(); const qq=eo3(seg(bt,3.5,5.0)); rr(c,1280,246,Math.max(16,530*qq),16,8); const gq=c.createLinearGradient(1280,0,1810,0); gq.addColorStop(0,PAL.bad); gq.addColorStop(.5,PAL.warn); gq.addColorStop(1,PAL.ok); c.fillStyle=gq; c.shadowColor=PAL.ok; c.shadowBlur=(14*qq)*.3; c.fill(); c.shadowBlur=0; txt(c,'100% excellent',1810,236,{size:20,w:600,mono:true,al:'right',col:PAL.ok}); c.restore(); }
};
// ---------------------------------------------------------------- every standard it decodes
const STD=[['DVB-T2','T2-Base and T2-Lite',['1.7 – 10 MHz channels','FFT 1K to 32K','QPSK to 256-QAM','Pilot patterns PP1–PP8']],
           ['DVB-T','First-generation terrestrial',['2K and 8K carriers','QPSK, 16- and 64-QAM','All guard intervals','All code rates']],
           ['ATSC 1.0','North American 8-VSB',['6 MHz channels','Adaptive equaliser (DFE)','Trellis + Reed-Solomon','PSIP channel names']]];
T.standards=(c,bt)=>{
  bgd(c,.9);
  c.save(); c.globalAlpha=.22; ridge(c,0,700,W,420,bt+6,[{u:.5,w:.3,k:1}],{rows:20,n:140,dt:.25}); c.restore();
  head(c,'Every standard.','Decoded in software.',bt,.2,{size:66,y:150});
  STD.forEach((cd,i)=>{ const x=110+i*600, y=330, w=560, h=470, k0=1.5+i, k=eo4(seg(bt,k0,k0+.8)); if(k<=0) return;
    c.save(); c.globalAlpha=clamp(k); c.translate(x+w/2,y+h/2+(1-clamp(k))*60); const pu=1+.015*pop(bt,k0); c.scale(pu,pu); c.translate(-(x+w/2),-(y+h/2));
    glass(c,x,y,w,h,24,{active:true}); sheen(c,cd[0],x+40,y+96,bt,k0+.1,{size:68,ls:-2}); txt(c,cd[1],x+42,y+138,{size:24,w:400,col:C.dim});
    cd[2].forEach((it,j)=>{ const a=eo5(seg(bt,k0+.4+j*.12,k0+.9+j*.12)); const yy=y+206+j*58; c.save(); c.globalAlpha=a; c.translate((1-a)*24,0); c.fillStyle=acc(.18); c.beginPath(); c.arc(x+56,yy-8,15,0,7); c.fill(); c.strokeStyle=PAL.accent; c.lineWidth=3; c.lineCap='round'; c.beginPath(); c.moveTo(x+49,yy-8); c.lineTo(x+54,yy-2); c.lineTo(x+64,yy-15); c.stroke(); txt(c,it,x+86,yy,{size:27,w:500}); c.restore(); });
    c.restore(); });
  [['Automatic standard detection',4.0],['Automatic bandwidth',4.5],['Transport stream out',5.0]].forEach(([s,t0],i)=>chip(c,s,[420,960,1450][i],900,eo4(seg(bt,t0,t0+.6)),{size:26,pop:pop(bt,t0)}));
};
// ---------------------------------------------------------------- built to be exact
T.exact=(c,bt)=>{
  bgd(c,.9);
  head(c,'Built to be exact.',null,bt,.2,{size:66,y:150});
  reveal(c,'A receiver you can trust, because it is checked against the reference.',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  const cards=[['Bit-exact','Coding chains checked against GNU Radio’s reference encoders.'],['Live-tested','Verified on live DVB-T2 broadcasts.'],['Real-time','LDPC on the GPU with Metal, or SIMD on the CPU.']];
  cards.forEach((cd,i)=>{ const x=110+i*600, y=310, w=560, h=560, k=eo4(seg(bt,1.0+i*1.0,1.8+i*1.0)); if(k<=0) return; const pu=1+.015*pop(bt,1+i);
    c.save(); c.globalAlpha=clamp(k); c.translate(x+w/2,y+h/2+(1-clamp(k))*60); c.scale(pu,pu); c.translate(-(x+w/2),-(y+h/2));
    glass(c,x,y,w,h,26,{active:i===0}); bigNum(c,cd[0],x+40,y+120,92,1); c.save(); c.font='400 28px Inter'; wrapText(c,cd[1],x+42,y+176,w-84,38,C.dim); c.restore();
    const gx=x+40,gy=y+260,gw=w-80,gh=h-300;
    if(i===0){ for(let r=0;r<5;r++){ const yy=gy+6+r*38; for(let q=0;q<9;q++){ const v=((hash(r*13+q*5)*255)|0).toString(16).padStart(2,'0').toUpperCase(); const scn=seg(bt,2.0,3.0)*10-r*1.6; const hl=clamp(scn)*(1-clamp(scn-1.2)); c.save(); if(hl>0){ c.fillStyle=acc(.22*hl); c.fillRect(gx+q*50-4,yy+2,44,30); } c.restore(); txt(c,v,gx+q*50,yy+24,{size:24,mono:true,col:scn>.5?PAL.ok:C.dim}); } }
      const ck=eo4(seg(bt,2.6,3.4)); c.save(); c.globalAlpha=clamp(ck); c.translate(gx+gw-30,gy+gh-34); c.scale(clamp(ck,0,1.3),clamp(ck,0,1.3)); c.fillStyle=PAL.ok; c.shadowColor=PAL.ok; c.shadowBlur=(24)*.3; c.beginPath(); c.arc(0,0,30,0,7); c.fill(); c.shadowBlur=0; c.strokeStyle='#04140b'; c.lineWidth=7; c.lineCap='round'; c.beginPath(); c.moveTo(-14,1); c.lineTo(-3,12); c.lineTo(15,-12); c.stroke(); c.restore(); txt(c,'identical',gx,gy+gh-18,{size:26,w:600,col:PAL.ok}); }
    else if(i===1){ plotFrame(c,gx,gy,gw,gh*.75); const sg=[{u:.5,w:.3,k:1}]; c.save(); c.beginPath(); c.rect(gx,gy,gw,gh*.75); c.clip(); c.strokeStyle=PAL.accent; c.lineWidth=2.4; c.shadowColor=PAL.accent; c.shadowBlur=(10)*.3; c.beginPath(); for(let j=0;j<=240;j++){ const u=j/240, d=specDb(u,bt+3,sg); const px=gx+u*gw, py=gy+gh*.75-clamp((d+100)/68)*gh*.75; j?c.lineTo(px,py):c.moveTo(px,py);} c.stroke(); c.restore(); txt(c,'live broadcast',gx,gy+gh-8,{size:26,mono:true,w:500,col:PAL.accent}); }
    else { for(let r=0;r<5;r++)for(let q=0;q<14;q++){ const on=clamp((bt-3.0)*14-(q+r)); const sh=.6+.4*Math.sin(bt*4+q*.6+r); c.fillStyle=on>0?acc(.4+.55*sh):'rgba(255,255,255,.07)'; c.fillRect(gx+q*37,gy+r*38,31,31); } txt(c,'GPU · SIMD',gx,gy+gh-8,{size:26,w:600,col:PAL.accent}); }
    c.restore(); });
};
// ---------------------------------------------------------------- see everything
T.see=(c,bt)=>{
  bgd(c,.9);
  head(c,'See what the radio sees.',null,bt,.2,{size:66,y:150});
  reveal(c,'Every stage of the receiver is visible, live.',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  const a1=eo5(seg(bt,1.2,2.0)); c.save(); c.globalAlpha=a1; c.translate(0,(1-a1)*40);
  glass(c,100,270,1110,590,22,{blur:40}); c.save(); rr(c,100,270,1110,590,22); c.clip(); ridge(c,120,285,1070,560,bt+5,[{u:.5,w:.3,k:1}],{rows:30,n:150,dt:.2}); c.restore(); txt(c,'Spectrum and waterfall',130,306,{size:20,col:C.dim}); c.restore();
  const a2=eo5(seg(bt,1.5,2.5)); c.save(); c.globalAlpha=a2; c.translate(0,(1-a2)*40); glass(c,1260,270,580,650,22,{blur:40}); constelFX(c,1290,300,520,64,eo3(seg(bt,2.2,6)),bt,{n:1800,dot:3.2,sigma:.05}); txt(c,'Constellation',1290,296,{size:20,col:C.dim});
  txt(c,'MER',1290,900,{size:20,col:C.dim}); txt(c,lerp(8.5,31.2,eo3(seg(bt,2.2,6))).toFixed(1)+' dB',1340,900,{size:30,w:500,mono:true,col:PAL.accent}); c.restore();
  const minis=[['Channel response',110],['SNR per carrier',500],['FEC blocks',890]]; minis.forEach(([nme,mx],i)=>{ const a=eo5(seg(bt,2.5+i*1.0,3.3+i*1.0)); if(a<=0) return; c.save(); c.globalAlpha=a; c.translate(0,(1-a)*30);
    const w=360,h=116,y=910; txt(c,nme,mx,y-12,{size:18,col:C.dim}); plotFrame(c,mx,y,w,h);
    if(i<2){ c.save(); c.beginPath(); c.rect(mx,y,w,h); c.clip(); c.strokeStyle=PAL.accent; c.lineWidth=1.8; c.beginPath(); for(let j=0;j<=160;j++){ const u=j/160; const v=i===0? .55+.12*Math.sin(u*14+bt)+vnoise(u*30)*.06 : .62+vnoise(u*60+bt*3)*.18; const px=mx+u*w, py=y+h-clamp(v)*h; j?c.lineTo(px,py):c.moveTo(px,py);} c.stroke(); c.restore(); }
    else { for(let r=0;r<6;r++)for(let q=0;q<20;q++){ const bad=hash(r*31+q*7+3)>.97; c.fillStyle=bad?PAL.bad:PAL.ok; c.globalAlpha=a*.85; c.fillRect(mx+8+q*17.2,y+8+r*16.4,13,13); } }
    c.restore(); });
};
// ---------------------------------------------------------------- fix reception
T.fix=(c,bt)=>{
  bgd(c,.9);
  const sw=eio(seg(bt,4.5,5.3));
  c.save(); c.globalAlpha=1-sw; head(c,'Know what’s wrong.',null,bt,.2,{size:66,y:150}); reveal(c,'Echoes, fades and drop-outs, found and explained.',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  const x=110,y=300,w=1520,h=560; glass(c,x-12,y-12,w+24,h+24,22,{blur:40}); plotFrame(c,x,y,w,h); c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip();
  c.strokeStyle='rgba(153,163,179,.16)'; c.lineWidth=1; for(let i=1;i<12;i++){c.beginPath();c.moveTo(x+i*w/12,y);c.lineTo(x+i*w/12,y+h);c.stroke()}
  const ux=u=>x+100+(u+1)*(w-160)/8; const gk=eo3(seg(bt,.8,1.6)); c.fillStyle='rgba(102,194,133,.08)'; c.fillRect(ux(0),y,(ux(7.0)-ux(0))*gk,h); c.strokeStyle='rgba(102,194,133,.5)'; c.setLineDash([6,6]); c.beginPath(); const gx=ux(0)+(ux(7.0)-ux(0))*gk; c.moveTo(gx,y); c.lineTo(gx,y+h); c.stroke(); c.setLineDash([]);
  txt(c,'guard interval',ux(0)+14,y+34,{size:18,col:PAL.ok,a:gk});
  const echoes=[[0,0],[1.1,-7],[2.3,-12],[4.4,-16],[5.8,-20]]; const base=y+h-46, scl=h-110;
  c.strokeStyle='#2c333a'; c.beginPath(); c.moveTo(x+40,base); c.lineTo(x+w-40,base); c.stroke(); for(let i=-1;i<=7;i++) txt(c,i+' µs',ux(i),base+30,{size:15,mono:true,col:C.dim,al:'center'});
  const pts=[]; for(let px=x+40;px<=x+w-40;px+=3){ let v=0; echoes.forEach(([d0,db],i)=>{ const kk=eo5(seg(bt,1.5+i*.5,2.1+i*.5)); const a=Math.pow(10,db/20)*kk; const dd=(px-ux(d0))/10; v+=a*Math.exp(-dd*dd); }); v+=Math.abs(vnoise(px*.07+bt*6))*.015; pts.push([px,base-v*scl]); }
  c.fillStyle=acc(.12); c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.lineTo(x+w-40,base); c.lineTo(x+40,base); c.closePath(); c.fill();
  c.strokeStyle=PAL.accent; c.lineWidth=2.8; c.shadowColor=PAL.accent; c.shadowBlur=(14)*.3; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke(); c.restore();
  echoes.forEach(([d0,db],i)=>{ const kk=eo5(seg(bt,1.5+i*.5,2.1+i*.5)); if(i>0&&kk>.5) txt(c,db+' dB',ux(d0),base-Math.pow(10,db/20)*scl-18,{size:20,w:600,mono:true,al:'center',col:i<3?PAL.warn:PAL.bad,a:kk}); });
  const b=eo4(seg(bt,3.3,3.9)); if(b>0){ c.save(); c.globalAlpha*=clamp(b); c.translate(1450,377); c.scale(clamp(b,0,1.15),clamp(b,0,1.15)); glass(c,-200,-47,400,94,16,{blur:30,top:'#2a2210',bot:'#17120a'}); c.strokeStyle=PAL.warn; c.lineWidth=2; rr(c,-199,-46,398,92,16); c.stroke(); txt(c,'multipath: likely',-172,-5,{size:32,w:600,col:PAL.warn}); txt(c,'echo −7 dB at +1.1 µs',-172,29,{size:19,mono:true,col:C.dim}); c.restore(); }
  c.restore();
  c.save(); c.globalAlpha=sw; c.translate((1-sw)*80,0);
  kinetic(c,'…and where to point',110,430,bt,5.0,{size:70,w:500,ls:-2.5,stagger:.02}); kinetic(c,'the antenna.',110,515,bt,5.25,{size:70,w:500,ls:-2.5,col:PAL.accent,stagger:.02}); reveal(c,'It measures every direction and tells you.',110,580,bt,5.7,{size:30,w:400,col:C.dim,ls:-.4});
  const cx=1390, cy=540, R=330;
  c.strokeStyle='#2c333a'; c.lineWidth=1.6; for(const r of [R,R*.5]){ c.beginPath(); c.arc(cx,cy,r,0,7); c.stroke(); }
  for(let d=0;d<360;d+=10){ const a=(d-90)*Math.PI/180, l=d%30===0?20:10; c.strokeStyle=d%90===0?'#77838f':'#3b434c'; c.lineWidth=d%30===0?2:1.2; c.beginPath(); c.moveTo(cx+Math.cos(a)*(R-l),cy+Math.sin(a)*(R-l)); c.lineTo(cx+Math.cos(a)*R,cy+Math.sin(a)*R); c.stroke(); }
  const dirs=['N','NE','E','SE','S','SW','W','NW'], score=[.38,.86,.52,.28,.22,.30,.41,.63];
  const swp=seg(bt,5.0,8.5); if(swp<1){ const ang=-Math.PI/2+Math.floor(swp*8)*Math.PI/4; c.strokeStyle='rgba(200,225,242,.55)'; c.lineWidth=1.6; c.beginPath(); c.moveTo(cx,cy); c.lineTo(cx+Math.cos(ang)*R,cy+Math.sin(ang)*R); c.stroke(); }
  dirs.forEach((dn,i)=>{ const a=-Math.PI/2+i*Math.PI/4, rch=seg(bt,5.0+i*.5,5.6+i*.5), best=i===1&&bt>8.5; const len=(R-50)*score[i]*eo4(rch), r0=40;
    c.save(); c.strokeStyle=best?PAL.ok:PAL.accent; c.globalAlpha=.9; c.lineWidth=11; c.lineCap='butt'; c.beginPath(); c.moveTo(cx+Math.cos(a)*r0,cy+Math.sin(a)*r0); c.lineTo(cx+Math.cos(a)*(r0+len),cy+Math.sin(a)*(r0+len)); c.stroke(); c.restore();
    txt(c,dn,cx+Math.cos(a)*(R+38),cy+Math.sin(a)*(R+38)+9,{size:i%2?20:26,w:600,al:'center',col:best?PAL.ok:(i%2?C.dim2==='x'?C.dim:'#6c7782':C.ink)});
    if(rch>.9) txt(c,Math.round(score[i]*100)+'',cx+Math.cos(a)*(r0+len+24),cy+Math.sin(a)*(r0+len+24)+6,{size:16,mono:true,al:'center',col:best?PAL.ok:C.ink}); });
  c.fillStyle='#141a20'; c.beginPath(); c.arc(cx,cy,34,0,7); c.fill(); c.strokeStyle='#39424a'; c.lineWidth=1.5; c.stroke();
  const fin=eo4(seg(bt,8.5,9.2)); if(fin>0){ c.save(); c.globalAlpha*=clamp(fin); c.translate(390,683); c.scale(clamp(fin,0,1.1),clamp(fin,0,1.1)); glass(c,-280,-43,560,86,16,{blur:30,top:'#13261b',bot:'#0b160f'}); c.strokeStyle=PAL.ok; c.lineWidth=2; rr(c,-279,-42,558,84,16); c.stroke(); txt(c,'Point it north-east.',-250,13,{size:38,w:600,col:PAL.ok}); c.restore(); }
  else { const bar=seg(bt,5.0,8.5); txt(c,'Turn to '+dirs[Math.min(7,Math.floor(bar*8))]+' and hold still…',110,700,{size:26,mono:true,col:C.dim}); rr(c,110,722,560,8,4); c.fillStyle='#14181c'; c.fill(); rr(c,110,722,560*bar,8,4); c.fillStyle=PAL.accent; c.fill(); }
  c.restore();
};
// ---------------------------------------------------------------- record / stream
T.out=(c,bt)=>{
  bgd(c,.9);
  head(c,'Record it. Stream it.','Send it anywhere.',bt,.2,{size:66,y:150});
  const k=eo5(seg(bt,.6,1.6)); c.save(); c.globalAlpha=k;
  const node=(x,y,w,h,title,sub,act)=>{ glass(c,x,y,w,h,22,{active:act,blur:36,dy:16}); txt(c,title,x+w/2,y+h/2-2,{size:34,w:600,al:'center'}); txt(c,sub,x+w/2,y+h/2+32,{size:19,col:C.dim,al:'center',mono:true}); };
  node(130,520,400,170,'OnAir','transport stream',true);
  const outs=[['.ts file','full mux or selected services',420],['UDP','unicast or multicast',605],['Live player','decoded in the app',790]]; const ox=1100;
  outs.forEach(([a,b,yy],i)=>{ const a2=eo4(seg(bt,1.5+i*.5,2.2+i*.5)); c.save(); c.globalAlpha=k*clamp(a2); c.translate((1-clamp(a2))*50,0); node(ox,yy-60,450,126,a,b,false); c.restore();
    c.strokeStyle=acc(.4*clamp(a2)); c.lineWidth=2; c.beginPath(); c.moveTo(530,605); c.bezierCurveTo(800,605,820,yy+3,ox,yy+3); c.stroke();
    for(let p=0;p<6;p++){ const u=((bt*.5+p/6+i*.13)%1); const xx=bez(530,800,820,ox,u), yy2=bez(605,605,yy+3,yy+3,u); c.save(); c.fillStyle='#bfe3fa'; c.shadowColor=PAL.accent; c.shadowBlur=(18)*.3; c.globalAlpha=k*clamp(a2)*Math.sin(u*Math.PI); c.beginPath(); c.arc(xx,yy2,5.5,0,7); c.fill(); c.restore(); } });
  ['VLC','ffplay','tvheadend'].forEach((n,i)=>chip(c,n,1700,545+i*60,eo4(seg(bt,3.0+i*.3,3.6+i*.3))*k,{size:21,pad:36}));
  c.restore();
};
// ---------------------------------------------------------------- outro
T.outro=(c,bt)=>{
  bgd(c,1);
  const lk=eo4(seg(bt,.2,1.0))*(1+.025*bp(bt)*seg(bt,1,1.5)); c.save(); c.translate(W/2,262); c.scale(lk,lk);
  const gl=c.createRadialGradient(0,0,0,0,0,360); gl.addColorStop(0,'rgba(224,97,87,.11)'); gl.addColorStop(1,'rgba(224,97,87,0)'); c.fillStyle=gl; c.fillRect(-400,-400,800,800);
  c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=60; c.shadowOffsetY=24; c.globalAlpha=seg(bt,.2,.6); c.drawImage(logo,-160,-160,320,320); c.restore();
  burst(c,W/2,262,bt,.2,70); shock(c,W/2,262,.2,bt);
  sheen(c,'OnAir',W/2,595,bt,1.0,{size:150,al:'center',ls:-6});
  kinetic(c,'Open-source digital TV receiver for your SDR.',W/2,665,bt,1.5,{size:40,w:400,col:C.dim,ls:-.8,stagger:.015,al:'center'});
  const row1=['DVB-T2','T2-Lite','DVB-T','ATSC'], w1=row1.map(s=>s.length*14+64), t1=w1.reduce((a,b)=>a+b+16,-16); let cx=W/2-t1/2;
  row1.forEach((s,i)=>{ chip(c,s,cx+w1[i]/2,740,eo4(seg(bt,2.0+i*.5,2.6+i*.5)),{size:22,pad:40,pop:pop(bt,2+i*.5)}); cx+=w1[i]+16; });
  const k=eo4(seg(bt,4.0,4.8)); c.save(); c.globalAlpha=clamp(k); c.translate(0,(1-clamp(k))*20);
  const plat=['macOS','Linux']; let px=W/2-(200+200+16)/2;
  plat.forEach((pl,i)=>{ glass(c,px,810,200,62,31,{active:true,blur:24,dy:8}); txt(c,pl,px+100,850,{size:28,w:600,al:'center'}); px+=216; });
  txt(c,'Runs natively on macOS and Linux*',W/2,920,{size:26,w:500,al:'center',col:C.ink}); c.restore();
  reveal(c,'github.com/adswill/OnAir',W/2,985,bt,5.0,{size:30,w:500,col:PAL.accent,mono:true,al:'center'});
  reveal(c,'* Windows coming soon.',W/2,1038,bt,5.5,{size:22,w:400,col:C.dim,al:'center'});
};

// ---------------------------------------------------------------- timeline: scene, bar-line start, bar-line end, transition into it, overlap
const B=[['hook',0,6,null,0],['logo',6,12,'zoom',.7],['radios',12,20,'dissolve',.8],['auto',20,28,'wipe',.8],['live',28,36,'push',.7],['standards',36,44,'zoom',.7],['exact',44,50,'dissolve',.7],['see',50,58,'wipe',.8],['fix',58,68,'zoom',.7],['out',68,74,'push',.7],['outro',74,82,'dissolve',.9]];
const TAG={radios:'ANY RADIO',auto:'AUTOMATIC',live:'LIVE TV',standards:'STANDARDS',exact:'EXACT',see:'SIGNAL',fix:'RECEPTION',out:'OUTPUT'};
const SCENES=B.map((s,i)=>{ const next=B[i+1]; const t0=s[1]-(s[4]||0)/2; const t1=s[2]+(next?next[4]/2:0); const o=[s[0],t1-t0,s[3],s[4]||0]; o.t0=t0; o.t1=t1; o.start=s[1]; o.idx=i; return o; });
const TOTAL=82; window.TOTAL=TOTAL; window.NF=Math.round(TOTAL*FPS);
window.SCENES=SCENES.map(s=>({name:s[0],t0:s.t0,t1:s.t1,tr:s[2],trd:s[3]}));
const bufA=mk(), bufB=mk(), bufT=mk(), ca=bufA.getContext('2d'), cb=bufB.getContext('2d'), ct=bufT.getContext('2d');
function sceneAt(c,s,t){ c.clearRect(0,0,W,H); const bt=t-s.start; CURBT=bt; CURT=t; c.save();
  if(s[0]!=='hook'){ const z=1+.003*clamp(bt+.6,0,12); c.translate(W/2+Math.sin(bt*.35)*6,H/2+Math.cos(bt*.3)*4); c.scale(z,z); c.translate(-W/2,-H/2); }
  T[s[0]](c,bt); c.restore();
  if(TAG[s[0]]){ const a=eo3(seg(bt,.5,1.1)); c.save(); c.globalAlpha=a*.8; txt(c,String(s.idx-1).padStart(2,'0')+'  ·  '+TAG[s[0]],W-110,84,{size:18,mono:true,w:500,col:C.dim,al:'right',ls:3}); c.restore(); } }
const bufM=mk(), cm=bufM.getContext('2d');
function trans(dst,type,p){
  const e=eio(p); dst.fillStyle='#000'; dst.fillRect(0,0,W,H);
  if(type==='dissolve'){ dst.drawImage(bufA,0,0); dst.save(); dst.globalAlpha=e; const s2=1.025-.025*e; dst.translate(W/2,H/2); dst.scale(s2,s2); dst.translate(-W/2,-H/2); dst.drawImage(bufB,0,0); dst.restore(); }
  else if(type==='wipe'){ dst.drawImage(bufA,0,0); const fe=420, xe=lerp(-fe*.2,W+fe,e); cm.globalCompositeOperation='source-over'; cm.clearRect(0,0,W,H); cm.drawImage(bufB,0,0); cm.globalCompositeOperation='destination-in'; const g=cm.createLinearGradient(xe-fe,0,xe,0); g.addColorStop(0,'rgba(0,0,0,1)'); g.addColorStop(1,'rgba(0,0,0,0)'); cm.fillStyle=g; cm.fillRect(0,0,W,H); cm.globalCompositeOperation='source-over'; dst.drawImage(bufM,0,0); }
  else if(type==='zoom'){ dst.save(); dst.globalAlpha=1-eo3(p); const s1=1+e*.18; dst.translate(W/2,H/2); dst.scale(s1,s1); dst.translate(-W/2,-H/2); dst.drawImage(bufA,0,0); dst.restore();
    dst.save(); dst.globalAlpha=eo3(p); const s2=1.1-e*.1; dst.translate(W/2,H/2); dst.scale(s2,s2); dst.translate(-W/2,-H/2); dst.drawImage(bufB,0,0); dst.restore(); }
  else if(type==='push'){ dst.save(); dst.translate(-e*W*.4,0); dst.globalAlpha=1-e*.85; dst.drawImage(bufA,0,0); dst.restore(); dst.save(); dst.translate((1-e)*W,0); dst.drawImage(bufB,0,0); dst.restore(); }
  else { dst.drawImage(bufA,0,0); dst.save(); dst.globalAlpha=e; dst.drawImage(bufB,0,0); dst.restore(); }
}
function composite(t){
  const act=SCENES.filter(s=>t>=s.t0&&t<s.t1);
  if(act.length===1){ sceneAt(ctx,act[0],t); window.CA=0; return; }
  const A=act[0],Bn=act[1], p=clamp((t-Bn.t0)/Bn[3]); sceneAt(ca,A,t); sceneAt(cb,Bn,t);
  // motion blur: average three sub-positions of the transition
  const ps=[p,clamp(p-.045),clamp(p-.09)]; ctx.clearRect(0,0,W,H);
  ps.forEach((pp,i)=>{ trans(ct,Bn[2],pp); ctx.globalAlpha=1/(i+1); ctx.drawImage(bufT,0,0); }); ctx.globalAlpha=1;
  window.CA=0;
}
// ---------------------------------------------------------------- post: bloom, chromatic aberration, vignette, grain, fade
const bl1=mk(480,270), bl1x=bl1.getContext('2d'), bl2=mk(240,135), bl2x=bl2.getContext('2d'), snap=mk(), snapx=snap.getContext('2d');
const grain=mk(256,256), gx_=grain.getContext('2d'); { const id=gx_.createImageData(256,256); for(let i=0;i<256*256;i++){ const v=(hash(i*1.37)*255)|0; id.data[i*4]=id.data[i*4+1]=id.data[i*4+2]=v; id.data[i*4+3]=255; } gx_.putImageData(id,0,0); }
let CA_OK=null; function testCA(){ try{ const a=mk(8,8), ax=a.getContext('2d'); ax.fillStyle='#fff'; ax.fillRect(0,0,8,8); const b=mk(8,8), bx=b.getContext('2d'); bx.filter='url(#fr)'; bx.drawImage(a,0,0); const d=bx.getImageData(4,4,1,1).data; CA_OK=(d[0]>200&&d[1]<40&&d[2]<40); }catch(e){ CA_OK=false; } }
function post(t,frame){
  // chromatic aberration while a transition runs
  if((window.CA||0)>.02){ if(CA_OK===null) testCA(); if(CA_OK){ snapx.clearRect(0,0,W,H); snapx.drawImage(cv,0,0); const d=window.CA*14; ctx.save(); ctx.fillStyle='#000'; ctx.fillRect(0,0,W,H); ctx.globalCompositeOperation='lighter';
      ctx.filter='url(#fr)'; ctx.drawImage(snap,-d,0); ctx.filter='url(#fg)'; ctx.drawImage(snap,0,0); ctx.filter='url(#fb)'; ctx.drawImage(snap,d,0); ctx.filter='none'; ctx.restore(); } }
  // bloom: contrast-stretched, heavily downsampled and blurred copy added back
  bl1x.filter='contrast(1.7) brightness(.78)'; bl1x.drawImage(cv,0,0,480,270); bl1x.filter='none';
  bl2x.filter='blur(5px)'; bl2x.clearRect(0,0,240,135); bl2x.drawImage(bl1,0,0,240,135); bl2x.filter='none';
  ctx.save(); ctx.globalCompositeOperation='lighter'; ctx.globalAlpha=.15; ctx.imageSmoothingQuality='high'; ctx.drawImage(bl2,0,0,W,H); ctx.restore();
  // vignette
  const g=ctx.createRadialGradient(W/2,H/2,H*.45,W/2,H/2,H*1.0); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.45)'); ctx.fillStyle=g; ctx.fillRect(0,0,W,H);
  // film grain
  ctx.save(); ctx.globalAlpha=.03; ctx.globalCompositeOperation='overlay'; const ox=(hash(frame*1.7)*256)|0, oy=(hash(frame*2.3)*256)|0; for(let x=-ox;x<W;x+=256) for(let y=-oy;y<H;y+=256) ctx.drawImage(grain,x,y); ctx.restore();
  const o=seg(t,TOTAL-0.9,TOTAL); if(o>0){ctx.fillStyle=`rgba(0,0,0,${o})`;ctx.fillRect(0,0,W,H)}
}
window.ready=Promise.all([document.fonts.load('500 20px Inter'),document.fonts.load('600 20px Inter'),document.fonts.load('400 20px Inter'),document.fonts.load('700 20px Inter'),document.fonts.load('500 20px "JetBrains Mono"'),logo.decode()]);
window.renderFrame=async(i)=>{ const t=i/FPS; composite(t); post(t,i); return t; };
