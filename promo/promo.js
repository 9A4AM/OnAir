// OnAir promo: deterministic canvas timeline. window.renderFrame(i) draws frame i (30 fps).
const W = 1920, H = 1080, FPS = 30;
const cv = document.getElementById('c'), ctx = cv.getContext('2d');
const C = { bg:'#050607', ink:'#f2f5f7', dim:'#7d8791', dim2:'#4a525a', blue:'#5cc0ff', blue2:'#2f86d6', green:'#5ef0a0', red:'#ff3b30', amber:'#ffb340',
  win:'#141619', frame:'#1d2126', line:'#2e3338', plot:'#030405' };
const clamp=(x,a=0,b=1)=>Math.min(b,Math.max(a,x));
const seg=(t,a,b)=>clamp((t-a)/(b-a));
const lerp=(a,b,k)=>a+(b-a)*k;
const eo3=k=>1-Math.pow(1-k,3), eo5=k=>1-Math.pow(1-k,5), eio=k=>k<.5?4*k*k*k:1-Math.pow(-2*k+2,3)/2;
const eoBack=k=>{const c1=1.70158,c3=c1+1;return 1+c3*Math.pow(k-1,3)+c1*Math.pow(k-1,2)};
const eoExp=k=>k>=1?1:1-Math.pow(2,-10*k);
const sstep=(a,b,x)=>{const k=clamp((x-a)/(b-a));return k*k*(3-2*k)};
function hash(n){n=Math.sin(n*127.1+311.7)*43758.5453;return n-Math.floor(n)}
function gauss(i){const a=hash(i*2+1),b=hash(i*2+2);return Math.sqrt(-2*Math.log(a+1e-9))*Math.cos(6.2831853*b)}
function vnoise(x){const i=Math.floor(x),f=x-i,u=f*f*(3-2*f);return lerp(hash(i),hash(i+1),u)*2-1}
function rr(c,x,y,w,h,r){c.beginPath();c.roundRect(x,y,w,h,r)}
function mk(w=W,h=H){const e=document.createElement('canvas');e.width=w;e.height=h;return e}
const logo=new Image(); logo.src='assets/logo.png';

function txt(c,s,x,y,o={}){
  const {size=40,w=500,col=C.ink,al='left',a=1,ls=0,mono=false,base='alphabetic'}=o;
  c.save(); c.globalAlpha*=a; c.font=`${w} ${size}px ${mono?'JetBrains Mono':'Inter'}`; c.textAlign=al; c.textBaseline=base;
  c.letterSpacing=ls+'px'; c.fillStyle=col; c.fillText(s,x,y); c.restore();
}
// staggered reveal: fade + rise
function reveal(c,s,x,y,t,t0,o={}){
  const k=eo5(seg(t,t0,t0+(o.dur||0.9))); if(k<=0)return;
  c.save(); c.translate(0,(1-k)*(o.rise??26)); txt(c,s,x,y,{...o,a:k*(o.a??1)}); c.restore();
}
function glowLine(c,pts,col,w=2,gl=1){
  c.save(); c.lineJoin='round'; c.lineCap='round';
  for(const [lw,al] of [[w*7,.06*gl],[w*3.5,.12*gl],[w,1]]){
    c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.strokeStyle=col; c.globalAlpha=al; c.lineWidth=lw; c.stroke();
  } c.restore();
}
function bigNum(c,s,x,y,size,k=1){
  const g=c.createLinearGradient(x,y-size,x+size*2.2,y); g.addColorStop(0,'#fff'); g.addColorStop(.45,'#cfeeff'); g.addColorStop(1,C.blue);
  c.save(); c.globalAlpha*=k; c.font=`600 ${size}px Inter`; c.letterSpacing=(-size*.035)+'px'; c.fillStyle=g; c.fillText(s,x,y); c.restore();
}

// ---------- signal model ----------
function floorDb(u,t){return -86+vnoise(u*90+t*9)*3+vnoise(u*23-t*2)*2}
function ofdm(u,t,c0,half){ // flat-top 8 MHz-ish block in dB
  const e=sstep(c0-half-0.012,c0-half+0.012,u)*(1-sstep(c0+half-0.012,c0+half+0.012,u));
  const rip=vnoise(u*140+t*14)*2.2+Math.sin(u*38+t*.7)*1.4;
  return -90+e*(43+rip);
}
function specDb(u,t,sigs){
  let v=floorDb(u,t);
  for(const s of sigs){
    if(s.type==='peak'){const d=(u-s.u)/s.w; v=Math.max(v,s.h*Math.exp(-d*d)+vnoise(t*8+s.u*50)*1.5-100+s.base)}
    else if(s.type==='ofdm'){v=Math.max(v,ofdm(u,t,s.u,s.w))}
  } return v;
}
// ---------- backdrop ----------
function bg(c,t=0,glow=0){
  c.fillStyle=C.bg; c.fillRect(0,0,W,H);
  if(glow>0){const g=c.createRadialGradient(W/2,H*.55,0,W/2,H*.55,W*.6); g.addColorStop(0,`rgba(40,110,170,${.16*glow})`); g.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g; c.fillRect(0,0,W,H);}
}
function drawSpectrum(c,x,y,w,h,t,sigs,col=C.blue,opt={}){
  const N=opt.n||420, pts=[]; const lo=opt.lo??-92, hi=opt.hi??-30;
  for(let i=0;i<=N;i++){const u=i/N; const d=specDb(u,t,sigs); pts.push([x+u*w,y+h-clamp((d-lo)/(hi-lo))*h]);}
  if(opt.fill){c.save();c.beginPath();pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1]));c.lineTo(x+w,y+h);c.lineTo(x,y+h);c.closePath();
    const g=c.createLinearGradient(0,y,0,y+h);g.addColorStop(0,opt.fill);g.addColorStop(1,'rgba(0,0,0,0)');c.fillStyle=g;c.fill();c.restore();}
  glowLine(c,pts,col,opt.lw||2,opt.gl??1); return pts;
}
// waterfall into ImageData
const wf=mk(360,150), wfx=wf.getContext('2d'), wfd=wfx.createImageData(360,150);
function cmap(v){ // v 0..1 -> blue..cyan..yellow
  v=clamp(v); const r=v<.55?0:(v-.55)/.45*255, g=v<.3?v/.3*60:v<.7?60+(v-.3)/.4*160:220+(v-.7)/.3*35, b=v<.3?40+v/.3*150:v<.7?190+(v-.3)/.4*40:230-(v-.7)/.3*180;
  return [r,g,b];
}
function drawWaterfall(c,x,y,w,h,t,sigs){
  const NW=360,NH=150, d=wfd.data;
  for(let r=0;r<NH;r++){const tau=t-r*.06; for(let i=0;i<NW;i++){const u=i/NW; const v=(specDb(u,tau,sigs)+92)/66; const [R,G,B]=cmap(Math.pow(clamp(v),1.4)*.95+.04); const o=(r*NW+i)*4; d[o]=R;d[o+1]=G;d[o+2]=B;d[o+3]=255;}}
  wfx.putImageData(wfd,0,0); c.save(); c.imageSmoothingEnabled=true; c.drawImage(wf,x,y,w,h); c.restore();
}
// ---------- constellation ----------
function drawConst(c,x,y,s,M,sigma,t,opt={}){
  const side=Math.sqrt(M), n=opt.n||(M<=4?260:900);
  c.save(); c.beginPath(); c.rect(x,y,s,s); c.clip(); c.fillStyle=C.plot; c.fillRect(x,y,s,s);
  const cell=s/(side+0.6);
  const pos=(i)=>{ if(M===2){return [(i&1?1:-1)*.35*s,0]} if(M===4){return [(i&1?1:-1)*.2*s,((i>>1)&1?1:-1)*.2*s]}
    const a=i%side,b=Math.floor(i/side)%side; return [(a-(side-1)/2)*cell,(b-(side-1)/2)*cell];};
  c.fillStyle=opt.col||'rgba(110,200,255,0.85)';
  for(let k=0;k<n;k++){
    const [px,py]=pos(Math.floor(hash(k*3.1+7)*M)); const ph=t*.5+k;
    const gx=gauss(k)*sigma*cell+Math.sin(ph)*sigma*cell*.12, gy=gauss(k+9999)*sigma*cell+Math.cos(ph*1.3)*sigma*cell*.12;
    c.fillRect(x+s/2+px+gx-1,y+s/2+py+gy-1,2.2,2.2);
  }
  c.strokeStyle='rgba(255,255,255,0.10)'; c.lineWidth=1; c.beginPath();
  if(M>4){for(let a=0;a<side;a++){const o=(a-(side-1)/2)*cell+cell/2*0; c.moveTo(x+s/2+o,y+4);c.lineTo(x+s/2+o,y+s-4);c.moveTo(x+4,y+s/2+o);c.lineTo(x+s-4,y+s/2+o);}}
  c.moveTo(x+s/2,y);c.lineTo(x+s/2,y+s);c.moveTo(x,y+s/2);c.lineTo(x+s,y+s/2);c.stroke();
  c.restore(); c.strokeStyle=C.line; c.lineWidth=1; c.strokeRect(x+.5,y+.5,s-1,s-1);
}
// ---------- UI window (base 1600x900) ----------
const LAMPS=['IQ','P1','GI','L1-pre','L1-post','Frame','LDPC','BCH','TS','Video','Audio'];
const LOG=['P1 found: T2-Base SISO 32K','Guard interval 1/128','L1-pre OK: PP7, 256-QAM L1','L1-post OK: 1 PLP, 3 services','Frame sync locked','LDPC: 6.2 it avg, 0 failed blocks','TS: 3 services, PAT/PMT/SDT parsed'];
function drawApp(c,s){
  const w=1600,h=900; c.save();
  rr(c,0,0,w,h,16); c.fillStyle=C.win; c.fill(); c.save(); rr(c,0,0,w,h,16); c.clip();
  // title bar
  c.fillStyle='#0f1114'; c.fillRect(0,0,w,34); ['#ff5f57','#febc2e','#28c840'].forEach((k,i)=>{c.fillStyle=k;c.beginPath();c.arc(22+i*22,17,6,0,7);c.fill()});
  txt(c,'OnAir',w/2,22,{size:13,w:500,col:C.dim,al:'center'});
  // toolbar
  const chip=(x,y,wd,label,val,hi)=>{rr(c,x,y,wd,28,5);c.fillStyle=C.frame;c.fill();txt(c,label,x+9,y+19,{size:12,col:C.dim});txt(c,val,x+wd-9,y+19,{size:13,w:500,col:hi||C.ink,al:'right',mono:true})};
  chip(14,46,230,'Source','HackRF Pro'); chip(254,46,230,'Frequency','522.000 MHz',C.blue); chip(494,46,150,'BW','Auto'); chip(654,46,110,'LNA','32'); chip(774,46,110,'VGA','20'); chip(894,46,110,'RF amp','on');
  rr(c,1014,46,92,28,5); c.fillStyle='#1a6ba8'; c.fill(); txt(c,'Start',1060,65,{size:13,w:600,al:'center'});
  rr(c,1116,46,92,28,5); c.fillStyle='#1d2126'; c.fill(); txt(c,'Stop',1162,65,{size:13,w:500,al:'center',col:C.dim});
  // lamps
  const lt=0.9; let x=16;
  LAMPS.forEach((n,i)=>{const on=clamp((s-lt-i*0.22)/0.2); const col=on>0?C.green:'#3a4047';
    c.fillStyle=col; c.beginPath(); c.arc(x+5,96,5,0,7); c.fill();
    if(on>0){c.save();c.globalAlpha=.35*(1-on*.4);c.shadowColor=C.green;c.shadowBlur=14;c.beginPath();c.arc(x+5,96,4,0,7);c.fill();c.restore()}
    txt(c,n,x+16,100,{size:12,col:on>0?C.ink:C.dim}); x+=16+c.measureText?(n.length*7.6+28):0;});
  const locked=clamp((s-lt-LAMPS.length*0.22)/0.3);
  txt(c,'Locked',1020,101,{size:13,w:600,col:C.green,a:locked}); txt(c,'DVB-T2  32K  1/128  PP7',1086,101,{size:13,w:500,mono:true,col:C.ink,a:locked});
  const snr=lerp(8,24.1,eo3(seg(s,1.2,5.2))); const k2=(l,v,col)=>[l,v,col];
  const m=[k2('CFO','+1234.5 Hz'),k2('SRO','-2.3 ppm'),k2('SNR',snr.toFixed(1)+' dB',C.green),k2('MER',(snr-.3).toFixed(1)+' dB'),k2('BER(pre)',(3*Math.pow(10,-(snr-8)/6)).toExponential(0).replace('e-','e-')),k2('LDPC it','6.2'),k2('Level','-18 dBFS')];
  let mx=16; m.forEach(([l,v,col])=>{txt(c,l,mx,127,{size:12,col:C.dim});mx+=l.length*7.2+8;txt(c,v,mx,127,{size:13,mono:true,col:col||C.ink,w:500});mx+=v.length*8.2+30;});
  // tabs
  ['Overview','TV','Receiver','Stream','Scan','Antenna','History'].forEach((n,i)=>{const tx=14+i*98; rr(c,tx,142,92,26,6); c.fillStyle=i===0?'#1a6ba8':'#1a1e22'; c.fill(); txt(c,n,tx+46,160,{size:13,w:500,al:'center',col:i===0?'#fff':C.dim});});
  c.strokeStyle=C.line; c.beginPath(); c.moveTo(0,174.5); c.lineTo(w,174.5); c.stroke();
  // spectrum + waterfall panel
  const sigs=[{type:'ofdm',u:.5,w:.32,k:1}];
  txt(c,'Spectrum  ·  522 MHz  ·  10 Msps  ·  occupied 7.6 MHz',20,198,{size:13,col:C.dim});
  c.fillStyle=C.plot; c.fillRect(14,208,880,260); c.strokeStyle=C.line; c.strokeRect(14.5,208.5,879,259);
  c.save(); c.beginPath(); c.rect(14,208,880,260); c.clip();
  c.strokeStyle='rgba(255,255,255,.07)'; for(let i=1;i<10;i++){c.beginPath();c.moveTo(14+i*88,208);c.lineTo(14+i*88,468);c.stroke()} for(let i=1;i<5;i++){c.beginPath();c.moveTo(14,208+i*52);c.lineTo(894,208+i*52);c.stroke()}
  c.fillStyle='rgba(92,192,255,.07)'; c.fillRect(14+880*.18,208,880*.64,260);
  drawSpectrum(c,14,208,880,260,s,sigs,C.blue,{n:300,fill:'rgba(92,192,255,.22)',lw:1.8}); c.restore();
  c.fillStyle=C.plot; c.fillRect(14,480,880,300); drawWaterfall(c,14,480,880,300,s,sigs); c.strokeStyle=C.line; c.strokeRect(14.5,480.5,879,299);
  ['518','520','522','524','526'].forEach((l,i)=>txt(c,l+' MHz',14+i*220+(i?0:30),795,{size:11,col:C.dim,mono:true,al:i?'center':'left'}));
  // constellations
  const sg=lerp(.55,.045,eo3(seg(s,1.6,5.4)));
  txt(c,'PLP 0  ·  256-QAM  ·  derotated',920,198,{size:13,col:C.dim});
  drawConst(c,920,208,380,256,sg,s,{n:2400});
  txt(c,'L1-post  QPSK',1316,198,{size:13,col:C.dim}); drawConst(c,1316,208,270,4,sg*1.4,s);
  txt(c,'L1-pre  BPSK',1316,500,{size:13,col:C.dim}); drawConst(c,1316,510,270,2,sg*1.4,s);
  // right-bottom: services
  txt(c,'Services',920,610,{size:13,col:C.dim});
  [['Channel One HD','H.264 · 5.4 Mb/s'],['Sports Live','H.264 · 4.1 Mb/s'],['Cinema Plus','HEVC · 3.8 Mb/s']].forEach((r,i)=>{const yy=622+i*38; rr(c,920,yy,380,32,5); c.fillStyle=i===0?'#1f3a52':C.frame; c.fill(); txt(c,r[0],932,yy+21,{size:14,w:500}); txt(c,r[1],1288,yy+21,{size:12,col:C.dim,al:'right',mono:true});});
  // log
  c.fillStyle='#0c0e10'; c.fillRect(0,806,w,94); c.strokeStyle=C.line; c.beginPath(); c.moveTo(0,806.5);c.lineTo(w,806.5);c.stroke();
  const nl=Math.min(LOG.length,Math.floor(Math.max(0,s-1)/0.55)); const start=Math.max(0,nl-4);
  for(let i=start;i<nl;i++) txt(c,`${(0.12+i*.31).toFixed(2)}s  ${LOG[i]}`,16,826+(i-start)*20,{size:12,mono:true,col:i===nl-1?C.ink:C.dim});
  c.restore(); c.restore();
  rr(c,0.5,0.5,w-1,h-1,16); c.strokeStyle='rgba(255,255,255,.14)'; c.lineWidth=1.5; c.stroke();
}
// perspective rotateY blit
function persp(c,src,cx,cy,sc,angDeg,alpha=1){
  const sw=src.width, sh=src.height, ang=angDeg*Math.PI/180, strip=3; c.save(); c.globalAlpha*=alpha;
  const f=2600;
  for(let x=0;x<sw;x+=strip){
    const lx=(x-sw/2)*sc; const z=lx*Math.sin(ang); const px=lx*Math.cos(ang); const s=f/(f+z);
    const h=sh*sc*s; c.drawImage(src,x,0,strip,sh,cx+px*s,cy-h/2,strip*sc*Math.cos(ang)*s+.6,h);
  } c.restore();
}
let appCv=null; function appBuf(){ if(!appCv){appCv=mk(1600,900)} return appCv; }

// ---------- scenes ----------
const T = {}; // scene fns: (c,t,d)
T.s1=(c,t,d)=>{
  bg(c,t,.0);
  const sigs=[{type:'peak',u:.31,w:.0035,h:eo5(seg(t,.9,2.2))*52,base:0},{type:'peak',u:.66,w:.004,h:eo5(seg(t,1.4,2.6))*30,base:0},{type:'peak',u:.12,w:.002,h:eo3(seg(t,1.8,2.6))*14,base:0},{type:'peak',u:.87,w:.003,h:eo3(seg(t,2,2.8))*18,base:0}];
  const k=seg(t,0,1.2);
  c.save(); c.globalAlpha=eo3(k); c.translate(0,0);
  // frequency grid
  c.strokeStyle='rgba(255,255,255,.05)'; for(let i=0;i<=16;i++){c.beginPath();c.moveTo(i*120,640);c.lineTo(i*120,900);c.stroke()}
  drawSpectrum(c,0,380,W,560,t,sigs,C.blue,{n:960,lo:-90,hi:-28,fill:'rgba(92,192,255,.10)',lw:2});
  c.restore();
  // tuner marker
  const mk1=eo5(seg(t,2.6,3.4)); const mx=W*.31;
  if(mk1>0){c.save();c.globalAlpha=mk1;c.strokeStyle='rgba(255,255,255,.35)';c.setLineDash([4,6]);c.beginPath();c.moveTo(mx,430);c.lineTo(mx,900);c.stroke();c.setLineDash([]);
    rr(c,mx+16,430,196,66,10);c.fillStyle='rgba(20,24,28,.9)';c.fill();c.strokeStyle=C.line;c.stroke();
    txt(c,'522.000 MHz',mx+32,461,{size:22,w:600,mono:true,col:C.ink}); txt(c,'DVB-T2 · 8 MHz',mx+32,485,{size:14,col:C.blue,mono:true}); c.restore();}
  reveal(c,'Somewhere,',90,170,t,.3,{size:76,w:500,ls:-2}); reveal(c,'someone is on the air.',90,258,t,.75,{size:76,w:500,ls:-2});
  reveal(c,'And it’s free to receive.',90,330,t,3.5,{size:36,w:400,col:C.dim,ls:-.5});
  txt(c,'MHz',W-70,970,{size:14,col:C.dim2,mono:true});
};
T.s2=(c,t,d)=>{
  bg(c,t,seg(t,1.6,2.6));
  // phase A: one radio
  const a=1-seg(t,1.5,2.0);
  if(a>0){ c.save(); c.globalAlpha=a;
    reveal(c,'One radio.',W/2,300,t,.1,{size:84,w:500,ls:-2.5,al:'center'});
    // HackRF-like board
    const bx=W/2-170, by=470, k=eo5(seg(t,.35,1.1)); c.globalAlpha=a*k; c.translate(0,(1-k)*30);
    rr(c,bx,by,340,150,14); c.fillStyle='#0e1114'; c.fill(); c.strokeStyle='#3a434b'; c.lineWidth=2; c.stroke();
    c.fillStyle='#1d2328'; rr(c,bx+16,by+16,150,118,8); c.fill(); txt(c,'HackRF',bx+34,by+66,{size:30,w:600}); txt(c,'One · Pro',bx+34,by+98,{size:18,col:C.dim});
    c.fillStyle='#b9c2cb'; c.fillRect(bx+340,by+44,40,10); c.fillRect(bx+372,by+36,10,26); c.fillStyle=C.green; c.beginPath(); c.arc(bx+300,by+30,6,0,7); c.fill();
    const pts=[]; for(let i=0;i<=200;i++){const x=bx-500+i*2.3; pts.push([x,by+75-Math.max(0,Math.exp(-Math.pow((i-70)/2.5,2))*60*eo3(seg(t,.5,1.2)))+vnoise(i*.7+t*9)*4])}
    glowLine(c,pts,C.blue,2); c.restore();
  }
  // phase B: logo
  const lk=eoBack(seg(t,1.9,2.7)); const ls=seg(t,1.9,2.4);
  if(ls>0){ c.save(); c.translate(W/2,400); const sc=.0+lk*1; c.scale(sc,sc);
    const gl=c.createRadialGradient(0,0,0,0,0,360); gl.addColorStop(0,'rgba(255,59,48,.20)'); gl.addColorStop(1,'rgba(255,59,48,0)'); c.fillStyle=gl; c.fillRect(-400,-400,800,800);
    c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=50; c.shadowOffsetY=20; c.globalAlpha=ls; c.drawImage(logo,-170,-170,340,340); c.restore(); }
  reveal(c,'OnAir',W/2,740,t,2.7,{size:104,w:600,ls:-4,al:'center'});
  reveal(c,'Turn a USB radio into a full digital TV receiver.',W/2,820,t,3.2,{size:40,w:400,col:C.dim,ls:-.5,al:'center'});
};
T.s3=(c,t,d)=>{
  bg(c,t,.9);
  const ab=appBuf(), ac=ab.getContext('2d'); ac.clearRect(0,0,1600,900); drawApp(ac,t);
  const k=eo5(seg(t,.1,1.7)); const zoomK=eio(seg(t,6.2,8.2)); const outK=eio(seg(t,8.6,9.6));
  const ang=lerp(-38,0,k); const sc=lerp(lerp(.7,.84,k),2.2,zoomK);
  const cx=lerp(960+lerp(160,0,k),278,zoomK), cy=lerp(610,791,zoomK);
  c.save(); c.globalAlpha=k;
  c.save(); c.globalAlpha=.4*k; const g=c.createRadialGradient(cx,cy,100,cx,cy,1100); g.addColorStop(0,'rgba(47,134,214,.22)'); g.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g; c.fillRect(0,0,W,H); c.restore();
  persp(c,ab,cx,cy,sc,ang);
  c.restore();
  // callouts
  const P=(ax,ay)=>[cx+(ax-800)*sc, cy+(ay-450)*sc];
  const calls=[['Lock lamps',[120,96],[-140,60],3.0],['Spectrum',[300,330],[-140,330],3.5],['Waterfall',[300,620],[-140,640],4.0],['Constellation',[1110,330],[1745,330],4.5],['Services',[1110,660],[1745,660],5.0]];
  calls.forEach(([n,tp,lp,t0])=>{ const a=eo5(seg(t,t0,t0+.6))*(1-seg(zoomK,0,.2)); if(a<=0)return; const [x1,y1]=P(...tp), [x2,y2]=P(...lp);
    c.save(); c.globalAlpha=a; c.strokeStyle='rgba(92,192,255,.8)'; c.lineWidth=2; c.beginPath(); c.moveTo(x1,y1); c.lineTo(x1+(x2-x1)*a,y1+(y2-y1)*a); c.stroke(); c.fillStyle=C.blue; c.beginPath(); c.arc(x1,y1,6,0,7); c.fill();
    c.font='600 22px Inter'; const tw=c.measureText(n).width+36; rr(c,x2-tw/2,y2-20,tw,40,20); c.fillStyle='rgba(8,14,20,.95)'; c.fill(); c.stroke(); txt(c,n,x2,y2+8,{size:22,w:600,al:'center'}); c.restore(); });
  const cap=1-seg(zoomK,0,.25);
  c.save(); c.globalAlpha=cap;
  reveal(c,'Every stage of the signal.',W/2,125,t,1.6,{size:62,w:500,ls:-2,al:'center'});
  reveal(c,'Visible.',W/2,196,t,2.1,{size:62,w:500,ls:-2,al:'center',col:C.blue}); c.restore();
  c.save(); c.globalAlpha=eo3(seg(t,7.6,8.4))*(1-outK); txt(c,'Constellations that tighten as the lock improves.',W/2,990,{size:34,w:400,col:C.ink,al:'center',ls:-.5});
  c.restore();
};
T.s4=(c,t,d)=>{
  bg(c,t,.5);
  reveal(c,'Auto-detect',160,150,t,.2,{size:24,w:500,col:C.blue,ls:1});
  reveal(c,'It knows what it’s looking at.',160,235,t,.4,{size:72,w:500,ls:-2.5});
  const cards=[{n:'DVB-T2',a:'T2-Base and T2-Lite',b:'1.7 – 10 MHz · up to 256-QAM',type:'ofdm',w:.34},{n:'DVB-T',a:'2K and 8K',b:'QPSK – 64-QAM · all guard intervals',type:'ofdm',w:.26},{n:'ATSC 1.0',a:'8-VSB',b:'6 MHz · decision-feedback equaliser',type:'vsb',w:.3}];
  const sel=t<2.2?-1:Math.min(2,Math.floor((t-2.2)/.7)); const allLit=seg(t,4.3,4.8);
  cards.forEach((cd,i)=>{
    const x=160+i*560, y=380, wd=520, ht=470, k=eo5(seg(t,.9+i*.18,1.9+i*.18)); if(k<=0)return;
    c.save(); c.globalAlpha=k; c.translate(0,(1-k)*50);
    const on=(sel===i)?1:0, lit=Math.max(on,allLit*.0);
    rr(c,x,y,wd,ht,22); c.fillStyle='#0b0e11'; c.fill(); c.strokeStyle=lit?'rgba(92,192,255,.7)':'#262b31'; c.lineWidth=lit?2:1.2; c.stroke();
    if(lit){c.save();c.shadowColor=C.blue;c.shadowBlur=36;c.globalAlpha=.4;rr(c,x,y,wd,ht,22);c.stroke();c.restore();}
    txt(c,cd.n,x+36,y+84,{size:58,w:600,ls:-2}); txt(c,cd.a,x+36,y+124,{size:24,col:C.ink,w:400}); txt(c,cd.b,x+36,y+158,{size:19,col:C.dim,w:400});
    // mini spectrum
    const gx=x+36,gy=y+200,gw=wd-72,gh=190; c.fillStyle=C.plot; rr(c,gx,gy,gw,gh,12); c.fill(); c.save(); rr(c,gx,gy,gw,gh,12); c.clip();
    const pts=[]; for(let j=0;j<=240;j++){const u=j/240; let v;
      if(cd.type==='ofdm'){const e=sstep(.5-cd.w-.02,.5-cd.w+.02,u)*(1-sstep(.5+cd.w-.02,.5+cd.w+.02,u)); v=.12+e*(.5+vnoise(u*120+t*12)*.04)+vnoise(u*50+t*3)*.02}
      else {const e=sstep(.5-cd.w-.02,.5-cd.w+.02,u)*(1-sstep(.5+cd.w-.02,.5+cd.w+.02,u)); v=.12+e*(.5+vnoise(u*140+t*14)*.05)+Math.exp(-Math.pow((u-(.5-cd.w+.01))/.008,2))*.32+vnoise(u*50+t*3)*.02}
      pts.push([gx+u*gw,gy+gh-clamp(v)*gh*1.15-10]);}
    glowLine(c,pts,on||allLit>0?C.blue:'#6f8ea6',2,on?1:.5); c.restore();
    // status line
    const ok=on||(sel>=0&&sel>i); txt(c,ok?'●  Detected':'○  Scanning',x+36,y+ht-30,{size:18,w:500,col:ok?C.green:C.dim2,mono:true});
    c.restore();
  });
  c.save(); c.globalAlpha=eo3(seg(t,4.4,5.2)); txt(c,'Standard, bandwidth, FFT size, guard interval — all automatic. Override any of it.',W/2,945,{size:30,w:400,col:C.dim,al:'center',ls:-.3}); c.restore();
};
function statScene(c,t,label,big,cap,drawRight,foot){
  bg(c,t,.0);
  reveal(c,label,160,380,t,.2,{size:30,w:500,col:C.green,ls:-.3});
  const k=eo5(seg(t,.35,1.4)); bigNum(c,big,155,570,230,k);
  reveal(c,cap,160,640,t,.9,{size:34,w:400,col:C.dim,ls:-.5});
  drawRight(c,t);
  if(foot) reveal(c,foot,160,990,t,1.6,{size:20,w:400,col:C.dim2,a:1});
}
T.s5=(c,t,d)=>{
  const n=Math.round(11*eo3(seg(t,.5,2.4)));
  statScene(c,t,'Transparent',String(n),'status lamps, from IQ to audio.',(c,t)=>{
    LAMPS.forEach((nm,i)=>{const x=1000, y=250+i*58, on=eo3(seg(t,.9+i*.2,1.3+i*.2)); rr(c,x,y,640,44,10); c.fillStyle='#0b0e11'; c.fill(); c.strokeStyle='#22272d'; c.stroke();
      c.fillStyle=on>0?C.green:'#2a3036'; c.beginPath(); c.arc(x+26,y+22,7,0,7); c.fill();
      if(on>0){c.save();c.shadowColor=C.green;c.shadowBlur=18;c.globalAlpha=.5*on;c.beginPath();c.arc(x+26,y+22,6,0,7);c.fill();c.restore()}
      txt(c,nm,x+52,y+29,{size:20,w:500,col:on>0?C.ink:C.dim}); const v=['10 Msps','found','1/128','OK','OK','locked','6.2 it','0 corr.','3 svc','H.264','AAC'][i]; txt(c,v,x+620,y+29,{size:17,mono:true,col:C.dim,al:'right',a:on});
      c.save();c.globalAlpha=on;c.fillStyle=C.green;c.fillRect(x,y+10,3,24);c.restore();});
  },'Every pipeline stage is shown live, so you always know where a weak signal breaks.');
};
T.s6=(c,t,d)=>{
  statScene(c,t,'Accelerated','GPU','LDPC decoding on Metal. AVX2 and NEON on the CPU.',(c,t)=>{
    const cols=26,rows=15, cs=28, gx=1000, gy=200; const wave=seg(t,.8,3.2)*(cols+rows+6);
    for(let r=0;r<rows;r++)for(let q=0;q<cols;q++){const on=clamp((wave-(q+r))/3); const sh=.6+.4*Math.sin(t*3+q*.5+r*.8+hash(q*31+r)*6); c.fillStyle=on>0?`rgba(${lerp(60,90,on)},${lerp(100,170+60*sh,on)},${lerp(140,230+25*sh,on)},${(.25+.75*on)*(.55+.45*sh)})`:'rgba(255,255,255,.06)'; c.fillRect(gx+q*cs,gy+r*cs,cs-4,cs-4);}
    // cpu small grid
    for(let r=0;r<4;r++)for(let q=0;q<7;q++){const on=clamp((seg(t,1.2,4.8)*14-(q+r))/2); c.fillStyle=on>0?'rgba(150,160,170,.7)':'rgba(255,255,255,.06)'; c.fillRect(1590+q*22,760+r*22,18,18);}
    c.fillStyle=C.blue; c.beginPath(); c.arc(1006,889,6,0,7); c.fill(); txt(c,'GPU (Metal)',1024,895,{size:20,col:C.ink}); c.fillStyle='#8c969f'; c.beginPath(); c.arc(1190,889,6,0,7); c.fill(); txt(c,'CPU fallback, always available',1208,895,{size:20,col:C.dim});
  },'Software-defined: no tuner chip, no capture card. Just a HackRF and your Mac.');
};
T.s7=(c,t,d)=>{
  bg(c,t,.6);
  reveal(c,'Watch it live.',160,150,t,.2,{size:72,w:500,ls:-2.5});
  reveal(c,'Video, audio, DVB subtitles, teletext and a programme guide.',160,205,t,.5,{size:28,w:400,col:C.dim,ls:-.3});
  const k=eo5(seg(t,.4,1.5)); c.save(); c.globalAlpha=k; c.translate(0,(1-k)*40);
  // TV window
  const x=160,y=270,w=1060,h=596; rr(c,x,y,w,h,22); c.fillStyle='#0b0e11'; c.fill(); c.strokeStyle='#2a2f35'; c.lineWidth=1.5; c.stroke();
  c.save(); rr(c,x+16,y+16,w-32,h-32,12); c.clip();
  const bars=['#c8c8c8','#c8c800','#00c8c8','#00c800','#c800c8','#c80000','#0000c8']; const bw=(w-32)/7; const ph=(h-32);
  bars.forEach((b,i)=>{c.fillStyle=b;c.fillRect(x+16+i*bw,y+16,bw+1,ph*.68)});
  ['#0000c8','#080808','#c800c8','#080808','#00c8c8','#080808','#c8c8c8'].forEach((b,i)=>{c.fillStyle=b;c.fillRect(x+16+i*bw,y+16+ph*.68,bw+1,ph*.08)});
  c.fillStyle='#10131a'; c.fillRect(x+16,y+16+ph*.76,w-32,ph*.24); c.fillStyle='#fff'; c.fillRect(x+16+bw*1.2,y+16+ph*.76,bw*1.4,ph*.24); c.fillStyle='#2a0f50'; c.fillRect(x+16+bw*2.7,y+16+ph*.76,bw*1.4,ph*.24);
  c.fillStyle='rgba(0,0,0,.18)'; for(let i=0;i<ph;i+=4)c.fillRect(x+16,y+16+i,w-32,2);
  // lower third
  const lt=eo5(seg(t,2.0,2.9)); c.save(); c.translate(-(1-lt)*500,0); rr(c,x+40,y+h-170,520,86,10); c.fillStyle='rgba(8,12,18,.9)'; c.fill(); c.fillStyle=C.red; c.fillRect(x+40,y+h-170,8,86);
  txt(c,'Channel One HD',x+70,y+h-125,{size:30,w:600}); txt(c,'Evening News · 18:00 – 18:30',x+70,y+h-96,{size:19,col:'#b9c4cf',w:400}); c.restore();
  rr(c,x+w-150,y+36,100,38,8); c.fillStyle='rgba(8,12,18,.85)'; c.fill(); txt(c,'CC  TXT',x+w-100,y+62,{size:18,w:600,al:'center',col:C.amber,mono:true});
  c.restore(); c.restore();
  // right column
  const rx=1280;
  const kk=eo5(seg(t,.9,1.9)); c.save(); c.globalAlpha=kk; c.translate((1-kk)*60,0);
  txt(c,'Signal quality',rx,300,{size:17,col:C.dim}); const q=eo3(seg(t,1.4,4.2)); rr(c,rx,316,480,18,9); c.fillStyle='#14181c'; c.fill();
  const g=c.createLinearGradient(rx,0,rx+480,0); g.addColorStop(0,C.red); g.addColorStop(.5,C.amber); g.addColorStop(1,C.green); c.save(); rr(c,rx,316,480*q,18,9); c.fillStyle=g; c.fill(); c.restore();
  txt(c,Math.round(q*96)+'%',rx+480,300,{size:20,w:600,al:'right',mono:true,col:q>.6?C.green:C.amber});
  txt(c,'Services',rx,392,{size:17,col:C.dim});
  [['101','Channel One HD'],['102','Sports Live'],['103','Cinema Plus'],['104','Music TV'],['201','Radio One']].forEach((r,i)=>{const yy=410+i*52; rr(c,rx,yy,480,44,9); c.fillStyle=i===0?'#1f3a52':'#0f1316'; c.fill(); txt(c,r[0],rx+16,yy+29,{size:17,mono:true,col:C.dim}); txt(c,r[1],rx+70,yy+29,{size:21,w:500}); if(i===0){c.fillStyle=C.red;c.beginPath();c.arc(rx+450,yy+22,6,0,7);c.fill()}});
  txt(c,'Now / Next',rx,722,{size:17,col:C.dim}); rr(c,rx,736,480,64,9); c.fillStyle='rgba(40,110,60,.45)'; c.fill(); txt(c,'18:00  Evening News',rx+16,764,{size:20,w:500}); txt(c,'18:30  Weather & Sport',rx+16,789,{size:17,col:C.dim});
  c.restore();
};
T.s8=(c,t,d)=>{
  bg(c,t,.5);
  reveal(c,'Where to point',160,440,t,.2,{size:80,w:500,ls:-3}); reveal(c,'the antenna?',160,530,t,.45,{size:80,w:500,ls:-3});
  reveal(c,'It measures every direction and tells you.',160,600,t,.9,{size:32,w:400,col:C.dim,ls:-.4});
  const cx=1370, cy=520, R=300; const k=eo5(seg(t,.3,1.3));
  c.save(); c.globalAlpha=k; c.strokeStyle='#262b31'; c.lineWidth=1.5; for(const r of [R,R*.66,R*.33]){c.beginPath();c.arc(cx,cy,r,0,7);c.stroke()}
  const dirs=['N','NE','E','SE','S','SW','W','NW']; const score=[.38,.86,.52,.28,.22,.30,.41,.63];
  dirs.forEach((dn,i)=>{const a=-Math.PI/2+i*Math.PI/4; const reach=seg(t,1.3+i*.34,1.9+i*.34); const best=i===1&&t>4.4;
    const len=R*score[i]*eo5(reach); const x1=cx+Math.cos(a)*len,y1=cy+Math.sin(a)*len;
    c.strokeStyle=best?C.green:'rgba(92,192,255,.85)'; c.lineWidth=best?16:12; c.lineCap='round'; c.beginPath(); c.moveTo(cx+Math.cos(a)*40,cy+Math.sin(a)*40); c.lineTo(x1,y1); c.stroke();
    txt(c,dn,cx+Math.cos(a)*(R+34),cy+Math.sin(a)*(R+34)+8,{size:22,w:600,al:'center',col:best?C.green:C.dim});
    if(reach>0.9) txt(c,Math.round(score[i]*100),x1+Math.cos(a)*24,y1+Math.sin(a)*24+6,{size:16,mono:true,al:'center',col:best?C.green:C.ink});});
  // sweeping needle
  const sw=seg(t,1.3,4.1); if(sw<1){const a=-Math.PI/2+sw*Math.PI*2*(7/8)*1.07; c.strokeStyle='rgba(255,255,255,.5)'; c.lineWidth=2; c.beginPath(); c.moveTo(cx,cy); c.lineTo(cx+Math.cos(a)*R*1.05,cy+Math.sin(a)*R*1.05); c.stroke();}
  c.fillStyle='#141a20'; c.beginPath(); c.arc(cx,cy,38,0,7); c.fill(); c.strokeStyle='#39424a'; c.stroke(); txt(c,'⌖',cx,cy+10,{size:32,al:'center',col:C.ink});
  c.restore();
  const fin=eo5(seg(t,4.3,5)); if(fin>0){c.save();c.globalAlpha=fin; rr(c,160,700,560,76,14); c.fillStyle='rgba(94,240,160,.12)'; c.fill(); c.strokeStyle=C.green; c.stroke(); txt(c,'Point it north-east.',190,750,{size:36,w:600,col:C.green}); c.restore();}
  else {const bar=seg(t,1.3,4.2); txt(c,'Turn to '+dirs[Math.min(7,Math.floor(bar*8))]+' and hold still…',160,740,{size:26,w:400,mono:true,col:C.dim}); rr(c,160,764,560,8,4); c.fillStyle='#14181c'; c.fill(); rr(c,160,764,560*bar,8,4); c.fillStyle=C.blue; c.fill();}
};
T.s9=(c,t,d)=>{
  bg(c,t,.4);
  reveal(c,'Echoes, fades, drop-outs.',160,170,t,.2,{size:68,w:500,ls:-2.5}); reveal(c,'Found for you.',160,250,t,.5,{size:68,w:500,ls:-2.5,col:C.amber});
  const x=160,y=340,w=1600,h=520; c.fillStyle=C.plot; rr(c,x,y,w,h,18); c.fill(); c.strokeStyle='#22272d'; c.stroke();
  c.save(); rr(c,x,y,w,h,18); c.clip();
  c.strokeStyle='rgba(255,255,255,.06)'; for(let i=1;i<12;i++){c.beginPath();c.moveTo(x+i*w/12,y);c.lineTo(x+i*w/12,y+h);c.stroke()}
  const ux=u=>x+120+(u+1)*(w-200)/8; // delay us -1..7
  // guard interval window
  const gk=eo3(seg(t,.9,1.8)); c.fillStyle='rgba(94,240,160,.07)'; c.fillRect(ux(0),y,(ux(2.8)-ux(0))*gk,h); c.strokeStyle='rgba(94,240,160,.45)'; c.setLineDash([6,6]); c.beginPath(); c.moveTo(ux(2.8)*gk+ux(0)*(1-gk),y); c.lineTo(ux(2.8)*gk+ux(0)*(1-gk),y+h); c.stroke(); c.setLineDash([]);
  txt(c,'guard interval',ux(0)+14,y+34,{size:16,col:C.green,a:gk});
  const echoes=[[0,0,C.blue],[1.1,-7,C.amber],[1.9,-12,C.amber],[3.6,-15,C.red],[5.2,-19,C.red]];
  const base=y+h-40; const dbH=db=>(1+db/24)*(h-120);
  c.strokeStyle='#2c333a'; c.beginPath(); c.moveTo(x+60,base); c.lineTo(x+w-60,base); c.stroke();
  for(let i=-1;i<=7;i++) txt(c,i+' µs',ux(i),base+26,{size:13,mono:true,col:C.dim,al:'center'});
  const pts=[]; for(let px=x+60;px<=x+w-60;px+=4){let v=0; echoes.forEach(([d0,db,col],i)=>{const kk=eo5(seg(t,1.4+i*.4,2.0+i*.4)); const a=Math.pow(10,db/20)*kk; const dd=(px-ux(d0))/9; v+=a*Math.exp(-dd*dd)}); v+=Math.abs(vnoise(px*.08+t*6))*.02; pts.push([px,base-v*(h-120)]);}
  glowLine(c,pts,C.blue,2.4);
  echoes.forEach(([d0,db,col],i)=>{const kk=eo5(seg(t,1.4+i*.4,2.0+i*.4)); if(i>0&&kk>.6){txt(c,db+' dB',ux(d0),base-Math.pow(10,db/20)*(h-120)-18,{size:18,w:600,mono:true,al:'center',col,a:kk}); }});
  c.restore();
  const b=eo5(seg(t,3.2,3.9)); if(b>0){c.save();c.globalAlpha=b;c.translate(0,(1-b)*20); rr(c,1340,380,380,92,14); c.fillStyle='rgba(255,179,64,.12)'; c.fill(); c.strokeStyle=C.amber; c.stroke();
    txt(c,'multipath: likely',1366,420,{size:30,w:600,col:C.amber}); txt(c,'echo -7 dB at +1.1 µs',1366,452,{size:18,mono:true,col:C.dim}); c.restore();}
};
T.s10=(c,t,d)=>{
  bg(c,t,.6);
  reveal(c,'Record it. Stream it.',160,170,t,.2,{size:72,w:500,ls:-2.5}); reveal(c,'Send it anywhere.',160,250,t,.5,{size:72,w:500,ls:-2.5,col:C.blue});
  const k=eo5(seg(t,.5,1.5)); c.save(); c.globalAlpha=k;
  const node=(x,y,w,h,title,sub,col)=>{rr(c,x,y,w,h,18);c.fillStyle='#0b0e11';c.fill();c.strokeStyle=col||'#2a2f35';c.lineWidth=1.6;c.stroke();txt(c,title,x+w/2,y+h/2-4,{size:30,w:600,al:'center'});txt(c,sub,x+w/2,y+h/2+30,{size:17,col:C.dim,al:'center',mono:true});};
  node(160,520,380,150,'OnAir','transport stream',C.blue);
  const outs=[['.ts file','full mux or selected services',400],['UDP','unicast or multicast',560],['Live player','VideoToolbox decode',720]];
  const ox=1100; outs.forEach(([a,b,yy],i)=>{ node(ox,yy-60+0,420,120,a,b);});
  // links
  outs.forEach(([a,b,yy],i)=>{ const y1=595, y2=yy; c.strokeStyle='rgba(92,192,255,.35)'; c.lineWidth=2; c.beginPath(); c.moveTo(540,y1); c.bezierCurveTo(800,y1,820,y2,ox,y2); c.stroke();
    for(let p=0;p<6;p++){ const u=((t*.5+p/6+i*.13)%1); const xx=bez(540,800,820,ox,u), yy2=bez(y1,y1,y2,y2,u); c.fillStyle=C.blue; c.globalAlpha=k*Math.sin(u*Math.PI); c.beginPath(); c.arc(xx,yy2,5,0,7); c.fill(); c.globalAlpha=k; } });
  ['VLC','ffplay','tvheadend'].forEach((n,i)=>{ const a=eo3(seg(t,2.2+i*.25,2.9+i*.25)); rr(c,1540+0,500+i*50,0,0,0); c.save();c.globalAlpha=a*k;rr(c,1550,520+i*52,200,40,20);c.fillStyle='#14181c';c.fill();txt(c,n,1650,547+i*52,{size:20,al:'center',col:C.dim,mono:true});c.restore();});
  // arrow from UDP to players
  c.strokeStyle='rgba(255,255,255,.2)'; c.lineWidth=2; c.beginPath(); c.moveTo(1520,560+0); c.lineTo(1550,560); c.stroke();
  c.restore();
};
function bez(a,b,c2,d,t){const u=1-t;return u*u*u*a+3*u*u*t*b+3*u*t*t*c2+t*t*t*d}
T.s11=(c,t,d)=>{
  bg(c,t,1);
  const lk=eoBack(seg(t,.3,1.1)); c.save(); c.translate(W/2,330); c.scale(lk,lk); c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=60; c.shadowOffsetY=24; c.globalAlpha=seg(t,.3,.7); c.drawImage(logo,-190,-190,380,380); c.restore();
  // tally pulse
  const p=.5+.5*Math.sin(t*3.2); const gg=c.createRadialGradient(W/2+105,350,0,W/2+105,350,220); gg.addColorStop(0,`rgba(255,59,48,${.18+.14*p})`); gg.addColorStop(1,'rgba(255,59,48,0)'); c.fillStyle=gg; c.globalAlpha=seg(t,.8,1.4); c.fillRect(W/2-300,150,800,400); c.globalAlpha=1;
  reveal(c,'OnAir',W/2,700,t,1.0,{size:170,w:600,ls:-7,al:'center'});
  reveal(c,'Open-source DVB-T2, DVB-T and ATSC receiver.',W/2,790,t,1.5,{size:42,w:400,col:C.dim,ls:-.8,al:'center'});
  const chips=['HackRF One','HackRF Pro','macOS','Linux','Free & open'];
  let tw=chips.map(s=>s.length*13+54), tot=tw.reduce((a,b)=>a+b+16,-16), cx=W/2-tot/2;
  chips.forEach((s,i)=>{const a=eo5(seg(t,2.0+i*.12,2.6+i*.12)); c.save();c.globalAlpha=a;c.translate(0,(1-a)*16); rr(c,cx,830,tw[i],46,23);c.strokeStyle='#2c333a';c.stroke();txt(c,s,cx+tw[i]/2,860,{size:20,al:'center',col:C.ink,w:500}); c.restore(); cx+=tw[i]+16;});
  reveal(c,'github.com/adswill/OnAir',W/2,960,t,3.2,{size:32,w:500,col:C.blue,mono:true,al:'center'});
};

// ---------- timeline ----------
const SCENES=[['s1',5.8,null,0],['s2',5.6,'iris',.7],['s3',9.6,'sweep',.8],['s4',6.2,'zoom',.7],['s5',5.4,'push',.7],['s6',5.4,'slices',.7],['s7',6.2,'iris',.8],['s8',5.4,'sweep',.7],['s9',5.0,'zoom',.7],['s10',4.6,'push',.7],['s11',7.6,'iris',.9]];
let cur=0; for(const s of SCENES){ s.st=cur-(s[3]||0)+ (cur===0?0:0); s.t0=cur===0?0:cur-s[3]; s.t1=s.t0+s[1]; cur=s.t1; }
const TOTAL=cur;
window.TOTAL=TOTAL; window.SCENES=SCENES.map(s=>({name:s[0],t0:s.t0,t1:s.t1,tr:s[2],trd:s[3]}));
const bufA=mk(), bufB=mk(), ca=bufA.getContext('2d'), cb=bufB.getContext('2d');
function sceneAt(c,s,t){ c.clearRect(0,0,W,H); T[s[0]](c,t-s.t0,s[1]); }
function composite(t){
  const act=SCENES.filter(s=>t>=s.t0&&t<s.t1);
  if(act.length===1){ sceneAt(ctx,act[0],t); return; }
  const A=act[0],B=act[1], p=clamp((t-B.t0)/B[3]); sceneAt(ca,A,t); sceneAt(cb,B,t);
  const e=eio(p), type=B[2];
  ctx.fillStyle='#000'; ctx.fillRect(0,0,W,H);
  if(type==='iris'){ ctx.drawImage(bufA,0,0); ctx.save(); const r=e*Math.hypot(W,H)*.75, ox=W/2, oy=H*.42; ctx.beginPath(); ctx.arc(ox,oy,r,0,7); ctx.clip(); ctx.drawImage(bufB,0,0); ctx.restore();
    ctx.save(); ctx.strokeStyle=C.blue; ctx.globalAlpha=(1-e)*.9; ctx.lineWidth=3; ctx.shadowColor=C.blue; ctx.shadowBlur=30; ctx.beginPath(); ctx.arc(ox,oy,r,0,7); ctx.stroke(); ctx.restore(); }
  else if(type==='sweep'){ ctx.drawImage(bufA,0,0); const x=lerp(-300,W+300,e), sk=240; ctx.save(); ctx.beginPath(); ctx.moveTo(0,0); ctx.lineTo(x+sk,0); ctx.lineTo(x-sk,H); ctx.lineTo(0,H); ctx.closePath(); ctx.clip(); ctx.drawImage(bufB,0,0); ctx.restore();
    ctx.save(); const g=ctx.createLinearGradient(x-90,0,x+90,0); g.addColorStop(0,'rgba(92,192,255,0)'); g.addColorStop(.5,'rgba(190,230,255,.55)'); g.addColorStop(1,'rgba(92,192,255,0)'); ctx.fillStyle=g; ctx.globalAlpha=Math.sin(p*Math.PI); ctx.beginPath(); ctx.moveTo(x+sk-90,0); ctx.lineTo(x+sk+90,0); ctx.lineTo(x-sk+90,H); ctx.lineTo(x-sk-90,H); ctx.closePath(); ctx.fill(); ctx.restore(); }
  else if(type==='zoom'){ ctx.save(); ctx.globalAlpha=1-eo3(p); const s1=1+e*.35; ctx.translate(W/2,H/2); ctx.scale(s1,s1); ctx.translate(-W/2,-H/2); ctx.drawImage(bufA,0,0); ctx.restore();
    ctx.save(); ctx.globalAlpha=eo3(p); const s2=1.18-e*.18; ctx.translate(W/2,H/2); ctx.scale(s2,s2); ctx.translate(-W/2,-H/2); ctx.drawImage(bufB,0,0); ctx.restore(); }
  else if(type==='push'){ ctx.save(); ctx.translate(-e*W*.45,0); ctx.globalAlpha=1-e*.8; ctx.drawImage(bufA,0,0); ctx.restore(); ctx.save(); ctx.translate((1-e)*W,0); ctx.drawImage(bufB,0,0); ctx.restore();
    ctx.save(); ctx.fillStyle='rgba(0,0,0,.5)'; ctx.globalAlpha=Math.sin(p*Math.PI)*.0; ctx.restore(); }
  else if(type==='slices'){ ctx.drawImage(bufA,0,0); const n=12, sw=W/n; for(let i=0;i<n;i++){ const q=eio(clamp((p*1.6)-i*.05*1.6/ (1) )) ; const qq=eo3(clamp(p*1.5-(i/n)*.5)); ctx.save(); ctx.beginPath(); ctx.rect(i*sw,H*(1-qq),sw+1,H*qq); ctx.clip(); ctx.drawImage(bufB,0,0); ctx.restore(); } }
  else { ctx.drawImage(bufA,0,0); ctx.save(); ctx.globalAlpha=e; ctx.drawImage(bufB,0,0); ctx.restore(); }
}
function post(t){
  // vignette + end fade + start fade
  const g=ctx.createRadialGradient(W/2,H/2,H*.45,W/2,H/2,H*1.0); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.45)'); ctx.fillStyle=g; ctx.fillRect(0,0,W,H);
  const f=1-seg(t,0,.5), o=seg(t,TOTAL-0.9,TOTAL); const a=Math.max(f,o); if(a>0){ctx.fillStyle=`rgba(0,0,0,${a})`;ctx.fillRect(0,0,W,H)}
}
window.ready=Promise.all([document.fonts.load('500 20px Inter'),document.fonts.load('600 20px Inter'),document.fonts.load('400 20px Inter'),document.fonts.load('300 20px Inter'),document.fonts.load('500 20px "JetBrains Mono"'),document.fonts.load('400 20px "JetBrains Mono"'),logo.decode()]);
window.renderFrame=(i)=>{ const t=i/FPS; composite(t); post(t); return t; };
window.NF=Math.round(TOTAL*FPS);
