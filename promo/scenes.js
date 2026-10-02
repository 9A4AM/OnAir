// OnAir promo v7: a calm, cinematic product film. Real-instrument visuals in the app's palette (single accent colour, navy waterfall
// ramp, accent-coloured constellations on near-black plots), flat hairline panels, light type, slow fades and dissolves only.
// Scene functions get bt = seconds since the scene's bar line. Every cue sits on the 120 BPM grid (see CUES, used by music.py).
Object.assign(C,{blue:'#5ca8db',green:'#66c285',amber:'#e6b347',red:'#e06157',ink:'#e6eaee',dim:'#8793a0',dim2:'#4d565f'});
const PAL={accent:'#5ca8db',ok:'#66c285',warn:'#e6b347',bad:'#e06157',grey:'#9eabba',plot:'#030405',border:'#363d45',panel:'#0c1014',faint:'#6c7782'};
const acc=a=>`rgba(92,168,219,${a})`;
let CURBT=0, CURT=0;
const eio3=k=>{k=clamp(k);return k<.5?4*k*k*k:1-Math.pow(-2*k+2,3)/2};
const ease=(bt,t0,d=.9)=>eio3(seg(bt,t0,t0+d*.6));

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
// ---------------------------------------------------------------- 1. title
T.title=(c,bt)=>{
  bgd(c,.35+.65*seg(bt,3,5));
  const sg=[{dat:'T',u:.5,w:.38,k:ease(bt,2.2,1.8)}];
  // the build: a carrier line grows from the centre, then the broadcast rises out of the noise floor
  const gw=eio3(seg(bt,.2,3.2))*W; c.save(); c.globalAlpha=.75*ease(bt,.1,1)*(1-.6*seg(bt,3.4,4.4)); c.strokeStyle=acc(.85); c.lineWidth=1.6; c.beginPath(); c.moveTo(W/2-gw/2,930); c.lineTo(W/2+gw/2,930); c.stroke(); c.restore();
  c.save(); c.globalAlpha=.55*ease(bt,1.6,2.2); spectrum(c,0,830,W,200,bt+3,sg,{n:500,lw:1.8,noFrame:true,grid:false,band:false}); c.restore();
  const hit=bt>4?Math.exp(-2.4*(bt-4)):0; if(false){ const g=c.createRadialGradient(W/2,400,0,W/2,400,760); g.addColorStop(0,`rgba(120,175,215,${.05+.20*hit})`); g.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g; c.fillRect(0,0,W,H); }
  fadeUp(c,bt,3.97,()=>{ c.save(); c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=50; c.shadowOffsetY=22; c.drawImage(logo,W/2-130,200,260,260); c.restore(); },{dur:.7,dy:10});
  line(c,'OnAir',W/2,650,bt,4.5,{size:124,w:500,ls:-4,al:'center',dur:.9});
  line(c,'Digital TV and radio, decoded in software.',W/2,734,bt,6.4,{size:38,w:300,col:C.ink,ls:.2,al:'center',dur:1.4});
  ['DVB-T2','DVB-T','ATSC','DAB','DAB+'].forEach((s,i)=>chip(c,s,W/2+(i-2)*170,822,ease(bt,7.0+i*.5,.8),{size:22,pad:38}));
};
// ---------------------------------------------------------------- 2. every standard
const STD=[['TELEVISION','DVB-T2','T2-Base and T2-Lite',['1.7 – 10 MHz channels','FFT 1K to 32K','QPSK to 256-QAM','Pilot patterns PP1–PP8']],
           ['TELEVISION','DVB-T','First-generation terrestrial',['2K and 8K carriers','QPSK, 16- and 64-QAM','All guard intervals','All code rates']],
           ['TELEVISION','ATSC 1.0','North American 8-VSB',['6 MHz channels','Adaptive equaliser (DFE)','Trellis + Reed-Solomon','PSIP channel names']],
           ['RADIO','DAB / DAB+','Digital radio ensembles',['DAB and DAB+ audio','1.5 MHz wide ensembles','Many stations per ensemble','Automatic station list']]];
T.standards=(c,bt)=>{
  bgd(c,.9);
  head(c,'Every broadcast.','One receiver.',bt,.2,{y:150});
  sub(c,'Television and digital radio, decoded in software.',bt,.7,258);
  const x0=120, cw=400, gap=24, y=330, h=470;
  txt(c,'TELEVISION',x0,y-18,{size:15,mono:true,col:PAL.faint,ls:3,a:ease(bt,.8)}); txt(c,'RADIO',x0+3*(cw+gap),y-18,{size:15,mono:true,col:PAL.faint,ls:3,a:ease(bt,.8)});
  STD.forEach((cd,i)=>{ const k0=1+i; fadeUp(c,bt,k0,()=>{ const x=x0+i*(cw+gap);
    panel(c,x,y,cw,h,{active:i===3}); txt(c,cd[1],x+30,y+76,{size:44,w:500,ls:-1}); txt(c,cd[2],x+31,y+114,{size:21,w:300,col:C.dim});
    cd[3].forEach((it,j)=>{ const a=ease(bt,k0+.4+j*.15,.7); c.save(); c.globalAlpha=a; const yy=y+196+j*58; check(c,x+44,yy-8,14); txt(c,it,x+72,yy,{size:23,w:400}); c.restore(); }); },{dur:1.0,dy:12}); });
  [['Automatic standard detection',5.0],['Automatic bandwidth',5.5],['Transport stream out',6.0]].forEach(([s,t0],i)=>chip(c,s,[420,960,1450][i],900,ease(bt,t0,.7),{size:24}));
};
// ---------------------------------------------------------------- 3. any radio (hardware)
const RADIOS=[['HackRF One / Pro','native driver','20',20],['Airspy','via SoapySDR','6–10',10],['SDRplay RSP','via SoapySDR','~10',10],['PlutoSDR','via SoapySDR','20+',20],['BladeRF','via SoapySDR','20+',20],['LimeSDR','via SoapySDR','20+',20],['USRP','via SoapySDR','20+',20],['RTL-SDR','narrow signals only','2.4',2.4]];
T.radios=(c,bt)=>{
  bgd(c,.9);
  head(c,'Bring your own radio.',null,bt,.2,{y:150});
  sub(c,'HackRF natively. Everything else through SoapySDR.',bt,.6,214);
  const lx=120, ly=290, cw=520, ch=104, gx=24, gy=20, logoC=[1560,560];
  fadeUp(c,bt,.6,()=>{ const gl=c.createRadialGradient(logoC[0],logoC[1],0,logoC[0],logoC[1],300); gl.addColorStop(0,'rgba(92,168,219,.12)'); gl.addColorStop(1,'rgba(92,168,219,0)'); c.fillStyle=gl; c.fillRect(logoC[0]-320,logoC[1]-320,640,640);
    c.save(); c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=40; c.shadowOffsetY=16; c.drawImage(logo,logoC[0]-110,logoC[1]-110,220,220); c.restore(); txt(c,'OnAir',logoC[0],logoC[1]+190,{size:40,w:500,al:'center'}); },{dur:1.2});
  RADIOS.forEach((r,i)=>{ const col=i%2, row=Math.floor(i/2); const x=lx+col*(cw+gx), y=ly+row*(ch+gy); const a=ease(bt,1.0+i*.5,.8); if(a<=0) return;
    const x1=x+cw, y1=y+ch/2; c.save(); c.globalAlpha=a*.28; c.strokeStyle=PAL.accent; c.lineWidth=1.2; c.beginPath(); c.moveTo(x1,y1); c.bezierCurveTo(x1+200,y1,logoC[0]-300,logoC[1],logoC[0]-150,logoC[1]); c.stroke(); c.restore();
    for(let p=0;p<0;p++){ const u=((bt*.28+p/3+i*.17)%1); const bx=bez(x1,x1+200,logoC[0]-300,logoC[0]-150,u), by=bez(y1,y1,logoC[1],logoC[1],u); c.save(); c.globalAlpha=a*Math.sin(u*Math.PI)*.7; c.fillStyle='#bcdcf0'; c.beginPath(); c.arc(bx,by,2.6,0,7); c.fill(); c.restore(); } });
  RADIOS.forEach((r,i)=>{ const col=i%2, row=Math.floor(i/2); const x=lx+col*(cw+gx), y=ly+row*(ch+gy); fadeUp(c,bt,1.0+i*.5,()=>{ panel(c,x,y,cw,ch,{active:i===0,blur:20,dy:8});
    txt(c,r[0],x+28,y+46,{size:29,w:500,ls:-.3}); txt(c,r[1],x+28,y+80,{size:19,col:i===0?PAL.accent:C.dim,w:300});
    const bx=x+cw-215, bw=170, by=y+60, k=ease(bt,1.3+i*.5,1.0); txt(c,r[2]+' Msps',bx+bw,y+36,{size:19,mono:true,w:500,al:'right',col:C.ink});
    rr(c,bx,by,bw,6,3); c.fillStyle='#1b2026'; c.fill(); rr(c,bx,by,Math.max(6,bw*(r[3]/20)*k),6,3); c.fillStyle=i===7?PAL.warn:PAL.accent; c.fill();
    c.strokeStyle='rgba(255,255,255,.4)'; c.lineWidth=1.2; c.beginPath(); c.moveTo(bx+bw*.5,by-6); c.lineTo(bx+bw*.5,by+12); c.stroke(); },{dy:8}); });
  line(c,'Bars: maximum sample rate (scale to 20 Msps).   Marker: about 10 Msps, enough for an 8 MHz channel.',130,1010,bt,5.5,{size:21,w:300,col:C.dim});
};
// ---------------------------------------------------------------- 4. watch TV
const tvBuf=mk(1140,690), tvx=tvBuf.getContext('2d');
T.tv=(c,bt)=>{
  bgd(c,.9);
  head(c,'Watch live TV.',null,bt,.1,{y:150});
  sub(c,'Video, audio, subtitles, teletext and a programme guide.',bt,.5,214);
  const k=ease(bt,.1,1.4);
  tvx.clearRect(0,0,1140,690); const x=40,y=40,w=1060,h=596; panel(tvx,x-16,y-16,w+32,h+32,{r:22,fill:'#10151a',blur:40,dy:18});
  tvx.save(); rr(tvx,x,y,w,h,12); tvx.clip(); tvPicture(tvx,x,y,w,h,bt,{lt:1.0});
  const sb=ease(bt,3.0,.7); if(sb>0){ tvx.globalAlpha=sb; rr(tvx,x+w*.17,y+h*.65,w*.66,h*.075,6); tvx.fillStyle='rgba(0,0,0,.78)'; tvx.fill(); txt(tvx,'…and now, the evening news.',x+w*.5,y+h*.65+h*.052,{size:h*.04,w:400,al:'center',col:'#fff'}); }
  tvx.restore(); const gls=tvx.createLinearGradient(x,y,x+w*.6,y+h); gls.addColorStop(0,'rgba(255,255,255,.07)'); gls.addColorStop(.35,'rgba(255,255,255,0)'); tvx.fillStyle=gls; rr(tvx,x,y,w,h,12); tvx.fill();
  c.save(); c.globalAlpha=k; persp(c,tvBuf,120+570,290+345,1,lerp(-14,0,k)); c.restore();
  fadeUp(c,bt,1.0,()=>{ const tx=1290,ty=290,tw=510,th=596; panel(c,tx-12,ty-12,tw+24,th+24,{r:16,fill:'#10151a'}); c.save(); rr(c,tx,ty,tw,th,6); c.clip(); c.fillStyle='#000'; c.fillRect(tx,ty,tw,th);
    const row=(n,s,col,bgc)=>{ const yy=ty+22+n*27; if(bgc){c.fillStyle=bgc;c.fillRect(tx+14,yy-21,tw-28,27);} txt(c,s,tx+22,yy,{size:21,mono:true,w:500,col}); };
    row(0,'P100  OnAir Text   18:04','#fff','#0000c8'); row(2,'NEWS','#ffeb3b'); row(3,'101 Headlines','#00e5ff'); row(4,'110 Politics','#00e5ff'); row(5,'120 World','#00e5ff'); row(7,'SPORT','#ffeb3b'); row(8,'200 Scores','#00e5ff'); row(9,'210 Fixtures','#00e5ff'); row(11,'WEATHER','#ffeb3b'); row(12,'300 Today','#00e5ff'); row(13,'310 Week ahead','#00e5ff');
    [['#e53935','NEWS'],['#43a047','SPORT'],['#fdd835','WEATHER'],['#00acc1','TV']].forEach(([cc,l],i)=>{ c.fillStyle=cc; c.fillRect(tx+14+i*(tw-28)/4,ty+th-60,(tw-28)/4-6,34); txt(c,l,tx+14+i*(tw-28)/4+((tw-28)/4-6)/2,ty+th-36,{size:17,mono:true,w:700,al:'center',col:'#000'}); });
    c.fillStyle='rgba(0,0,0,.14)'; for(let yy=0;yy<th;yy+=4) c.fillRect(tx,ty+yy+2,tw,2); c.restore(); },{dur:1.0,dy:10});
  ['Live video + audio','Programme guide','DVB subtitles','Teletext'].forEach((s,i)=>chip(c,s,[330,740,1150,1520][i],985,ease(bt,1.5+i*.5,.7),{size:22}));
  const q=ease(bt,4.0,.8); if(q>0){ c.save(); c.globalAlpha=q; txt(c,'Signal quality',1290,236,{size:19,col:C.dim}); rr(c,1290,246,510,12,6); c.fillStyle='#161b21'; c.fill(); rr(c,1290,246,Math.max(12,510*ease(bt,4.1,1.4)),12,6); const gq=c.createLinearGradient(1290,0,1800,0); gq.addColorStop(0,PAL.bad); gq.addColorStop(.5,PAL.warn); gq.addColorStop(1,PAL.ok); c.fillStyle=gq; c.fill(); txt(c,'100% excellent',1800,236,{size:19,w:500,mono:true,al:'right',col:PAL.ok}); c.restore(); }
};
// ---------------------------------------------------------------- 5. listen: DAB / DAB+
const STATIONS=[['City Radio','DAB+','AAC','64 kbit/s'],['Classic Hits','DAB+','AAC','72 kbit/s'],['Talk 24','DAB','MP2','128 kbit/s'],['Jazz & Soul','DAB+','AAC','64 kbit/s'],['Kids Zone','DAB+','AAC','48 kbit/s']];
T.radio=(c,bt)=>{
  bgd(c,.9);
  head(c,'And digital radio.',null,bt,.2,{y:150});
  sub(c,'DAB and DAB+ ensembles, decoded and played.',bt,.6,214);
  fadeUp(c,bt,1.0,()=>{ const x=120,y=300,w=830,h=300; txt(c,'Band III  ·  6 MHz view',x,y-14,{size:18,col:C.dim}); const sg=[{dat:'D',u:.27,w:.128,k:1},{dat:'D',u:.72,w:.128,k:ease(bt,1.6,1.2)}];
    plotFrame(c,x,y,w,h); spectrum(c,x,y,w,h,bt+4,sg,{n:380});
    const ex=x+.27*w; c.strokeStyle=acc(.8); c.lineWidth=1.4; c.beginPath(); c.moveTo(ex,y+h*.3); c.lineTo(ex,y+34); c.stroke(); txt(c,'Ensemble · 1.536 MHz',ex,y+26,{size:20,w:500,col:PAL.accent,al:'center'});
    txt(c,'Waterfall',x,y+h+66,{size:18,col:C.dim}); waterfall(c,x,y+h+76,w,150,bt+4,sg); c.strokeStyle=PAL.border; c.lineWidth=1.2; c.strokeRect(x+.6,y+h+76.6,w-1.2,148.8); },{dur:1.1});
  const lx=1010, ly=300, lw=790;
  txt(c,'Stations',lx,ly-14,{size:18,col:C.dim,a:ease(bt,1.8)});
  STATIONS.forEach((s,i)=>{ fadeUp(c,bt,2.0+i*.5,()=>{ const y=ly+i*84, sel=i===0; panel(c,lx,y,lw,72,{r:8,blur:12,dy:4,active:sel,fill:sel?'#0e1a24':PAL.panel});
    txt(c,s[0],lx+24,y+44,{size:28,w:500}); const tg=(t,xx)=>{ c.font='500 17px Inter'; const tw=c.measureText(t).width+22; rr(c,xx,y+22,tw,28,14); c.fillStyle=acc(.14); c.fill(); c.strokeStyle=acc(.5); c.lineWidth=1; c.stroke(); txt(c,t,xx+11,y+42,{size:17,w:500,col:PAL.accent}); return tw; };
    const w1=tg(s[1],lx+330); c.font='500 17px Inter'; const tw2=c.measureText(s[2]).width+22; rr(c,lx+330+w1+10,y+22,tw2,28,14); c.fillStyle='rgba(255,255,255,.05)'; c.fill(); c.strokeStyle='rgba(255,255,255,.2)'; c.stroke(); txt(c,s[2],lx+330+w1+21,y+42,{size:17,w:500,col:C.ink});
    txt(c,s[3],lx+lw-24,y+44,{size:20,mono:true,al:'right',col:C.dim}); if(sel){ c.fillStyle=PAL.accent; c.beginPath(); c.moveTo(lx+lw-170,y+24); c.lineTo(lx+lw-170,y+48); c.lineTo(lx+lw-150,y+36); c.closePath(); c.fill(); } },{dur:.8,dy:8}); });
  fadeUp(c,bt,4.5,()=>{ const x=120,y=830,w=830; txt(c,'City Radio',x,y-6,{size:26,w:500}); txt(c,'now playing',x+210,y-6,{size:19,col:C.dim});
    ['L','R'].forEach((ch,i)=>{ const yy=y+22+i*34; txt(c,ch,x,yy+14,{size:16,mono:true,col:C.dim}); rr(c,x+30,yy,w-30,16,3); c.fillStyle='#10151a'; c.fill();
      const lvl=tt=>clamp(.52+.2*vnoise(tt*9+i*7)+.14*vnoise(tt*23+i*3)+.06*vnoise(tt*41+i)); const lv=lvl(bt); let pk=0; for(let j=0;j<8;j++) pk=Math.max(pk,lvl(bt-j*.1)-j*.012); const n=60, sg_=(w-30)/n; for(let q=0;q<n;q++){ if(q/n<lv){ c.fillStyle=q/n>.85?PAL.bad:q/n>.7?PAL.warn:PAL.accent; c.globalAlpha=.9; c.fillRect(x+30+q*sg_+1,yy+2,sg_-2,12); } } c.globalAlpha=1; c.fillStyle='#d8e4ee'; c.fillRect(x+30+Math.min(n-1,Math.floor(pk*n))*sg_+1,yy+2,sg_-2,12); }); },{dur:.8});
  [['DAB and DAB+',5.5],['Station list',6.0],['Audio playback',6.5]].forEach(([s,t0],i)=>chip(c,s,[1170,1420,1680][i],960,ease(bt,t0,.7),{size:21}));
};
// ---------------------------------------------------------------- 6. automatic
T.auto=(c,bt)=>{
  bgd(c,.9);
  head(c,'It finds the signal.','You don’t configure a thing.',bt,.2,{y:150});
  const items=[['Detects the standard','DVB-T2, DVB-T, ATSC, DAB'],['Measures the bandwidth','1.5 to 10 MHz, automatically'],['Reads the signalling','FFT size, guard interval, pilots'],['Locks and decodes','then keeps tracking drift']];
  items.forEach(([a,b],i)=>fadeUp(c,bt,1.0+i*.5,()=>{ const y=370+i*118; panel(c,120,y-40,560,98,{blur:16,dy:6}); check(c,162,y+9,19); txt(c,a,204,y+4,{size:29,w:500,ls:-.3}); txt(c,b,204,y+40,{size:20,w:300,col:C.dim}); },{dy:8}));
  fadeUp(c,bt,2.8,()=>{ const x=760,y=420,w=1040,h=330; txt(c,'Channel scanner',x,y-70,{size:28,w:500}); txt(c,'VHF and UHF',x+w,y-70,{size:18,mono:true,col:C.dim,al:'right'});
    const found=[[.16,'DAB',.045],[.40,'DVB-T2',.09],[.64,'DVB-T',.09],[.86,'ATSC',.06]]; const sw=seg(bt,3.0,5.2);
    plotFrame(c,x,y,w,h); c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip(); const pts=[]; for(let i=0;i<=520;i++){ const u=i/520; let d=floorDb(u,bt); for(const m of found){ if(sw>m[0]-.01) d=Math.max(d,ofdm(u,bt,m[0],m[2])); } pts.push([x+u*w,y+h-clamp((d+100)/68)*h]); }
    const tr=c.createLinearGradient(x+w*sw-w*.1,0,x+w*sw,0); tr.addColorStop(0,'rgba(92,168,219,0)'); tr.addColorStop(1,'rgba(92,168,219,.2)'); c.fillStyle=tr; c.fillRect(x+w*Math.max(0,sw-.1),y,w*Math.min(.1,sw),h);
    c.strokeStyle=PAL.accent; c.lineWidth=2; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke(); c.strokeStyle='rgba(215,235,248,.7)'; c.lineWidth=1.5; c.beginPath(); c.moveTo(x+w*sw,y); c.lineTo(x+w*sw,y+h); c.stroke(); c.restore();
    found.forEach(([u,l],i)=>{ const a=ease(bt,3.0+i*.5,.6); if(a<=0) return; c.save(); c.globalAlpha=a; const px=x+u*w; c.strokeStyle=PAL.ok; c.lineWidth=1.6; c.beginPath(); c.moveTo(px,y+h*.22); c.lineTo(px,y-6); c.stroke(); txt(c,l,px,y-16,{size:19,w:600,mono:true,al:'center',col:PAL.ok}); c.restore(); }); },{dur:.9});
};
// ---------------------------------------------------------------- 7. see everything
T.see=(c,bt)=>{
  bgd(c,.9);
  head(c,'See what the radio sees.',null,bt,.1,{y:150});
  sub(c,'Every stage of the receiver is visible, live.',bt,.6,214);
  const sigs=[{dat:'T',u:.5,w:.38,k:1}];
  fadeUp(c,bt,1.0,()=>{ const x=160,y=300,w=1040; txt(c,'Spectrum and waterfall',x,y-14,{size:18,col:C.dim}); spectrum(c,x,y,w,270,bt+5,sigs,{n:420,axes:true,db:true,fs:14,f0:598}); waterfall(c,x,y+310,w,240,bt+5,sigs); c.strokeStyle=PAL.border; c.lineWidth=1.2; c.strokeRect(x+.6,y+310.6,w-1.2,238.8); },{dur:1.1});
  fadeUp(c,bt,2.0,()=>{ txt(c,'Constellation',1290,286,{size:18,col:C.dim}); constel(c,1290,300,510,64,lerp(.6,.05,ease(bt,2.2,3.8)),bt,{n:1800,dot:3}); txt(c,'MER',1290,846,{size:19,col:C.dim}); txt(c,lerp(8.5,31.2,ease(bt,2.2,3.8)).toFixed(1)+' dB',1345,846,{size:28,w:500,mono:true,col:PAL.accent}); },{dur:1.1});
  [['Channel response',160,3.0],['SNR per carrier',560,3.5],['FEC blocks',960,4.0]].forEach(([nme,mx,t0],i)=>fadeUp(c,bt,t0,()=>{ const w=360,h=112,y=920; txt(c,nme,mx,y-12,{size:17,col:C.dim}); plotFrame(c,mx,y,w,h);
    if(i<2){ c.save(); c.beginPath(); c.rect(mx,y,w,h); c.clip(); c.strokeStyle=PAL.accent; c.lineWidth=1.6; c.beginPath(); for(let j=0;j<=160;j++){ const u=j/160; const v=i===0?.55+.12*Math.sin(u*14+bt)+vnoise(u*30)*.06:.62+vnoise(u*60+bt*3)*.18; const px=mx+u*w, py=y+h-clamp(v)*h; j?c.lineTo(px,py):c.moveTo(px,py);} c.stroke(); c.restore(); }
    else { for(let r=0;r<6;r++)for(let q=0;q<20;q++){ const bad=hash(r*31+q*7+3)>.97; c.fillStyle=bad?PAL.bad:PAL.ok; c.globalAlpha=.85; c.fillRect(mx+8+q*17.2,y+8+r*16,13,13); } c.globalAlpha=1; } },{dy:8}));
};
// ---------------------------------------------------------------- 8. fix reception
T.fix=(c,bt)=>{
  bgd(c,.9);
  const sw=eio3(seg(bt,4.5,5.5));
  c.save(); c.globalAlpha=1-sw; head(c,'Know what’s wrong.',null,bt,.2,{y:150}); sub(c,'Echoes, fades and drop-outs, found and explained.',bt,.6,214);
  fadeUp(c,bt,.8,()=>{ const x=120,y=300,w=1560,h=560; plotFrame(c,x,y,w,h); c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip();
    c.strokeStyle='rgba(153,163,179,.14)'; c.lineWidth=1; for(let i=1;i<12;i++){c.beginPath();c.moveTo(x+i*w/12,y);c.lineTo(x+i*w/12,y+h);c.stroke()}
    const ux=u=>x+100+(u+1)*(w-160)/8; const gk=ease(bt,.8,.9); c.fillStyle='rgba(102,194,133,.07)'; c.fillRect(ux(0),y,(ux(7.0)-ux(0))*gk,h); c.strokeStyle='rgba(102,194,133,.45)'; c.setLineDash([6,6]); c.beginPath(); const gx=ux(0)+(ux(7.0)-ux(0))*gk; c.moveTo(gx,y); c.lineTo(gx,y+h); c.stroke(); c.setLineDash([]);
    txt(c,'guard interval',ux(0)+14,y+34,{size:18,col:PAL.ok,a:gk});
    const echoes=[[0,0],[1.1,-7],[2.3,-12],[4.4,-16],[5.8,-20]]; const base=y+h-46, scl=h-110;
    c.strokeStyle='#2c333a'; c.beginPath(); c.moveTo(x+40,base); c.lineTo(x+w-40,base); c.stroke(); for(let i=-1;i<=7;i++) txt(c,i+' µs',ux(i),base+30,{size:15,mono:true,col:C.dim,al:'center'});
    const pts=[]; for(let px=x+40;px<=x+w-40;px+=3){ let v=0; echoes.forEach(([d0,db],i)=>{ const kk=ease(bt,1.5+i*.5,.7); const a=Math.pow(10,db/20)*kk; const dd=(px-ux(d0))/10; v+=a*Math.exp(-dd*dd)*(1+.12*Math.cos(dd*3.2)); }); v+=Math.abs(gauss(px*3.1+Math.floor(bt*8)*977))*.010; pts.push([px,base-v*scl]); }
    c.fillStyle=acc(.10); c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.lineTo(x+w-40,base); c.lineTo(x+40,base); c.closePath(); c.fill();
    c.strokeStyle=PAL.accent; c.lineWidth=2.2; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke(); c.restore();
    echoes.forEach(([d0,db],i)=>{ const kk=ease(bt,1.5+i*.5,.7); if(i>0&&kk>.4) txt(c,db+' dB',ux(d0),base-Math.pow(10,db/20)*scl-18,{size:20,w:600,mono:true,al:'center',col:i<3?PAL.warn:PAL.bad,a:kk}); }); },{dur:1.0});
  fadeUp(c,bt,3.5,()=>{ panel(c,1250,330,400,94,{r:10,fill:'#1c1709',blur:14,dy:4}); c.strokeStyle=PAL.warn; c.lineWidth=1.4; rr(c,1250.7,330.7,398.6,92.6,10); c.stroke(); txt(c,'multipath: likely',1278,372,{size:31,w:500,col:PAL.warn}); txt(c,'echo −7 dB at +1.1 µs',1278,406,{size:19,mono:true,col:C.dim}); },{dy:6});
  c.restore();
  c.save(); c.globalAlpha=sw; c.translate((1-sw)*30,0);
  line(c,'…and where to point',120,430,bt,5.2,{size:66,w:400,ls:-1.2}); line(c,'the antenna.',120,512,bt,5.4,{size:66,w:400,ls:-1.2,col:PAL.accent}); line(c,'It measures every direction and tells you.',120,578,bt,5.8,{size:28,w:300,col:C.dim});
  const cx=1390, cy=540, R=330;
  c.strokeStyle='#2c333a'; c.lineWidth=1.4; for(const r of [R,R*.5]){ c.beginPath(); c.arc(cx,cy,r,0,7); c.stroke(); }
  for(let d=0;d<360;d+=10){ const a=(d-90)*Math.PI/180, l=d%30===0?20:10; c.strokeStyle=d%90===0?'#77838f':'#3b434c'; c.lineWidth=d%30===0?1.8:1.1; c.beginPath(); c.moveTo(cx+Math.cos(a)*(R-l),cy+Math.sin(a)*(R-l)); c.lineTo(cx+Math.cos(a)*R,cy+Math.sin(a)*R); c.stroke(); }
  const dirs=['N','NE','E','SE','S','SW','W','NW'], score=[.38,.86,.52,.28,.22,.30,.41,.63];
  const swp=seg(bt,5.0,8.5); if(swp<1){ const ang=-Math.PI/2+Math.floor(swp*8)*Math.PI/4; c.strokeStyle='rgba(200,225,242,.5)'; c.lineWidth=1.4; c.beginPath(); c.moveTo(cx,cy); c.lineTo(cx+Math.cos(ang)*R,cy+Math.sin(ang)*R); c.stroke(); }
  dirs.forEach((dn,i)=>{ const a=-Math.PI/2+i*Math.PI/4, rch=ease(bt,5.0+i*.5,.6), best=i===1&&bt>8.5; const len=(R-50)*score[i]*rch, r0=40;
    c.save(); c.strokeStyle=best?PAL.ok:PAL.accent; c.globalAlpha=.9; c.lineWidth=10; c.lineCap='butt'; c.beginPath(); c.moveTo(cx+Math.cos(a)*r0,cy+Math.sin(a)*r0); c.lineTo(cx+Math.cos(a)*(r0+len),cy+Math.sin(a)*(r0+len)); c.stroke(); c.restore();
    txt(c,dn,cx+Math.cos(a)*(R+38),cy+Math.sin(a)*(R+38)+9,{size:i%2?19:25,w:500,al:'center',col:best?PAL.ok:(i%2?PAL.faint:C.ink)});
    if(rch>.9) txt(c,Math.round(score[i]*100)+'',cx+Math.cos(a)*(r0+len+24),cy+Math.sin(a)*(r0+len+24)+6,{size:16,mono:true,al:'center',col:best?PAL.ok:C.ink}); });
  c.fillStyle='#12181e'; c.beginPath(); c.arc(cx,cy,34,0,7); c.fill(); c.strokeStyle='#39424a'; c.lineWidth=1.4; c.stroke();
  const fin=ease(bt,8.5,.8); if(fin>0){ c.save(); c.globalAlpha*=fin; panel(c,120,640,540,84,{r:10,fill:'#0e1d14',blur:12,dy:4}); c.strokeStyle=PAL.ok; c.lineWidth=1.4; rr(c,120.7,640.7,538.6,82.6,10); c.stroke(); txt(c,'Point it north-east.',150,696,{size:36,w:500,col:PAL.ok}); c.restore(); }
  else { const bar=seg(bt,5.0,8.5); txt(c,'Turn to '+dirs[Math.min(7,Math.floor(bar*8))]+' and hold still…',120,690,{size:25,mono:true,col:C.dim}); rr(c,120,712,540,6,3); c.fillStyle='#14181c'; c.fill(); rr(c,120,712,540*bar,6,3); c.fillStyle=PAL.accent; c.fill(); }
  c.restore();
};
// ---------------------------------------------------------------- 9. built to be exact
T.exact=(c,bt)=>{
  bgd(c,.9);
  head(c,'Built to be exact.',null,bt,.2,{y:150});
  sub(c,'A receiver you can trust, because it is checked against the reference.',bt,.6,214);
  const cards=[['Bit-exact','Coding chains checked against GNU Radio’s reference encoders.'],['Live-tested','Verified on live DVB-T2 broadcasts.'],['Real-time','LDPC on the GPU with Metal, or SIMD on the CPU.']];
  cards.forEach((cd,i)=>fadeUp(c,bt,1.0+i*1.0,()=>{ const x=120+i*600, y=310, w=560, h=560;
    panel(c,x,y,w,h,{r:12,active:i===0}); c.save(); c.font='500 74px Inter'; c.letterSpacing='-2px'; c.fillStyle='#eef2f5'; c.fillText(cd[0],x+40,y+116); c.restore(); c.save(); c.font='300 27px Inter'; wrapText(c,cd[1],x+42,y+172,w-84,38,C.dim); c.restore();
    const gx=x+40,gy=y+260,gw=w-80,gh=h-300;
    if(i===0){ for(let r=0;r<5;r++){ const yy=gy+6+r*38; for(let q=0;q<9;q++){ const v=((hash(r*13+q*5)*255)|0).toString(16).padStart(2,'0').toUpperCase(); const scn=seg(bt,2.0,3.0)*10-r*1.6; txt(c,v,gx+q*50,yy+24,{size:24,mono:true,col:scn>.5?PAL.ok:C.dim}); } } check(c,gx+gw-28,gy+gh-30,24,PAL.ok); txt(c,'identical',gx,gy+gh-14,{size:25,w:500,col:PAL.ok}); }
    else if(i===1){ plotFrame(c,gx,gy,gw,gh*.75); const sg=[{dat:'T',u:.5,w:.38,k:1}]; c.save(); c.beginPath(); c.rect(gx,gy,gw,gh*.75); c.clip(); c.strokeStyle=PAL.accent; c.lineWidth=2; c.beginPath(); for(let j=0;j<=240;j++){ const u=j/240, d=specDb(u,bt+3,sg); const px=gx+u*gw, py=gy+gh*.75-clamp((d+100)/68)*gh*.75; j?c.lineTo(px,py):c.moveTo(px,py);} c.stroke(); c.restore(); txt(c,'live broadcast',gx,gy+gh-8,{size:24,mono:true,w:500,col:PAL.accent}); }
    else { for(let r=0;r<5;r++)for(let q=0;q<14;q++){ const on=clamp((bt-3.0)*14-(q+r)); c.fillStyle=on>0?acc(.55+.3*Math.sin(bt*3+q*.6+r)):'rgba(255,255,255,.07)'; c.fillRect(gx+q*37,gy+r*38,31,31); } txt(c,'GPU · SIMD',gx,gy+gh-8,{size:24,w:500,col:PAL.accent}); } },{dur:1.0,dy:12}));
};
// ---------------------------------------------------------------- 10. record / stream
T.out=(c,bt)=>{
  bgd(c,.9);
  head(c,'Record it. Stream it.','Send it anywhere.',bt,.2,{y:150});
  const k=ease(bt,.6,1.0); c.save(); c.globalAlpha=k;
  const node=(x,y,w,h,title,subt,act)=>{ panel(c,x,y,w,h,{r:12,active:act,blur:20,dy:8}); txt(c,title,x+w/2,y+h/2-2,{size:33,w:500,al:'center'}); txt(c,subt,x+w/2,y+h/2+32,{size:18,col:C.dim,al:'center',mono:true}); };
  node(130,520,400,170,'OnAir','transport stream',true);
  const outs=[['.ts file','full mux or selected services',420],['UDP','unicast or multicast',605],['Live player','decoded in the app',790]]; const ox=1100;
  outs.forEach(([a,b,yy],i)=>{ const a2=ease(bt,1.5+i*.5,.8); c.save(); c.globalAlpha=k*a2; node(ox,yy-60,450,126,a,b,false); c.restore();
    c.strokeStyle=acc(.34*a2); c.lineWidth=1.4; c.beginPath(); c.moveTo(530,605); c.bezierCurveTo(800,605,820,yy+3,ox,yy+3); c.stroke();
    for(let p=0;p<0;p++){ const u=((bt*.4+p/5+i*.13)%1); const xx=bez(530,800,820,ox,u), yy2=bez(605,605,yy+3,yy+3,u); c.save(); c.fillStyle='#bcdcf0'; c.globalAlpha=k*a2*Math.sin(u*Math.PI)*.8; c.beginPath(); c.arc(xx,yy2,3.2,0,7); c.fill(); c.restore(); } });
  ['VLC','ffplay','tvheadend'].forEach((n,i)=>chip(c,n,1700,545+i*60,ease(bt,3.0+i*.3,.6)*k,{size:20,pad:34}));
  c.restore();
};
// ---------------------------------------------------------------- finale: all of it, at once
T.finale=(c,bt)=>{
  bgd(c,1.3);
  const cols=[120,690,1260], rows=[250,610], w=540, h=330, sig=[{dat:'T',u:.5,w:.38,k:1}];
  const dim=1-.5*ease(bt,4.0,.7);
  const P=(i,fn)=>{ const x=cols[i%3], y=rows[Math.floor(i/3)]; fadeUp(c,bt,1.0+i*.5,()=>{ c.save(); c.globalAlpha*=dim; panel(c,x,y,w,h,{r:10,blur:18,dy:6}); c.save(); rr(c,x,y,w,h,10); c.clip(); fn(x,y); c.restore(); c.restore(); },{dur:.7,dy:10}); };
  P(0,(x,y)=>tvPicture(c,x,y,w,h,bt,{lt:1.5}));
  P(1,(x,y)=>{ txt(c,'Stations',x+22,y+34,{size:17,col:C.dim}); STATIONS.slice(0,3).forEach((s,i)=>{ const yy=y+52+i*54, sel=i===0; rr(c,x+16,yy,w-32,46,8); c.fillStyle=sel?'#0e1a24':'#0a0e12'; c.fill(); if(sel){c.strokeStyle='rgba(110,180,225,.6)'; c.lineWidth=1.2; c.stroke();} txt(c,s[0],x+34,yy+31,{size:22,w:500}); txt(c,s[1]+' · '+s[2],x+w-34,yy+30,{size:16,mono:true,al:'right',col:PAL.accent}); });
    ['L','R'].forEach((ch,i)=>{ const yy=y+232+i*30; txt(c,ch,x+22,yy+13,{size:14,mono:true,col:C.dim}); const lv=clamp(.55+.18*Math.sin(bt*5.3+i*1.9)+.12*Math.sin(bt*11.7+i*.7)); const n=40, sw_=(w-76)/n; for(let q=0;q<n;q++){ c.fillStyle=q/n<lv?(q/n>.85?PAL.bad:q/n>.7?PAL.warn:PAL.accent):'#10151a'; c.fillRect(x+46+q*sw_+1,yy,sw_-2,15); } }); });
  P(2,(x,y)=>{ spectrum(c,x+12,y+12,w-24,160,bt+2,sig,{n:240,noFrame:false}); waterfall(c,x+12,y+184,w-24,134,bt+2,sig); });
  P(3,(x,y)=>{ constel(c,x+(w-300)/2,y+16,300,64,.05+.012*Math.sin(bt),bt,{n:900,dot:2.4}); txt(c,'MER',x+(w-300)/2,y+h-8,{size:15,col:C.dim}); txt(c,'31.2 dB',x+(w-300)/2+48,y+h-8,{size:20,mono:true,w:500,col:PAL.accent}); });
  P(4,(x,y)=>{ const bx=x+24, bw=w-48, base=y+h-48, sc=h-120; c.strokeStyle='#2c333a'; c.beginPath(); c.moveTo(bx,base); c.lineTo(bx+bw,base); c.stroke(); c.fillStyle='rgba(102,194,133,.07)'; c.fillRect(bx+bw*.1,y+16,bw*.6,base-y-16);
    c.strokeStyle=PAL.accent; c.lineWidth=2; c.beginPath(); for(let px=0;px<=bw;px+=3){ let v=0; [[.1,1],[.2,.45],[.32,.25],[.55,.16]].forEach(([u,a])=>{ const dd=(px-u*bw)/8; v+=a*Math.exp(-dd*dd); }); v+=Math.abs(vnoise(px*.07+bt*6))*.015; const yy=base-v*sc; px?c.lineTo(bx+px,yy):c.moveTo(bx+px,yy); } c.stroke(); txt(c,'echo −7 dB',bx+bw*.2,base-sc*.45-14,{size:16,mono:true,col:PAL.warn,al:'center'}); txt(c,'multipath: likely',x+24,y+h-14,{size:18,w:500,col:PAL.warn}); });
  P(5,(x,y)=>{ const cx=x+w/2, cy=y+h/2-4, R=124; c.strokeStyle='#2c333a'; c.lineWidth=1.2; c.beginPath(); c.arc(cx,cy,R,0,7); c.stroke(); c.beginPath(); c.arc(cx,cy,R*.5,0,7); c.stroke();
    for(let d=0;d<360;d+=15){ const a=(d-90)*Math.PI/180, l=d%45===0?12:6; c.strokeStyle='#3b434c'; c.beginPath(); c.moveTo(cx+Math.cos(a)*(R-l),cy+Math.sin(a)*(R-l)); c.lineTo(cx+Math.cos(a)*R,cy+Math.sin(a)*R); c.stroke(); }
    [.38,.86,.52,.28,.22,.30,.41,.63].forEach((s,i)=>{ const a=-Math.PI/2+i*Math.PI/4, len=(R-26)*s; c.strokeStyle=i===1?PAL.ok:PAL.accent; c.lineWidth=7; c.beginPath(); c.moveTo(cx+Math.cos(a)*22,cy+Math.sin(a)*22); c.lineTo(cx+Math.cos(a)*(22+len),cy+Math.sin(a)*(22+len)); c.stroke(); }); txt(c,'N',cx,cy-R-8,{size:15,w:600,al:'center',col:C.ink}); });
  // the headline lands on the climax hit
  const k=ease(bt,4.0,.9); if(k>0){ c.save(); c.globalAlpha=k; const bg=c.createLinearGradient(0,380,0,700); bg.addColorStop(0,'rgba(5,7,10,0)'); bg.addColorStop(.5,'rgba(5,7,10,.72)'); bg.addColorStop(1,'rgba(5,7,10,0)'); c.fillStyle=bg; c.fillRect(0,380,W,320);
    txt(c,'All of it. At once.',W/2,566+(1-k)*10,{size:112,w:400,ls:-3,al:'center'}); const ps=['DVB-T2','DVB-T','ATSC','DAB','DAB+']; ps.forEach((s,i)=>chip(c,s,W/2+(i-2)*170,640,ease(bt,4.6+i*.2,.6),{size:22,pad:38})); c.restore(); }
};
// ---------------------------------------------------------------- 11. outro
T.outro=(c,bt)=>{
  bgd(c,1);
  fadeUp(c,bt,.2,()=>{ c.save(); c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=50; c.shadowOffsetY=22; c.drawImage(logo,W/2-120,120,240,240); c.restore(); },{dur:1.6,dy:12});
  line(c,'OnAir',W/2,540,bt,1.2,{size:110,w:500,ls:-4,al:'center',dur:1.6});
  line(c,'Digital TV and radio, decoded in software.',W/2,610,bt,2.4,{size:36,w:300,col:C.dim,al:'center',dur:1.6});
  ['DVB-T2','DVB-T','ATSC','DAB','DAB+'].forEach((s,i)=>chip(c,s,W/2+(i-2)*170,700,ease(bt,3.5+i*1.0,1.0),{size:22,pad:38}));
  fadeUp(c,bt,9.0,()=>{ ['macOS','Linux'].forEach((pl,i)=>{ const px=W/2-212+i*216; panel(c,px,770,200,60,{r:30,active:true,blur:10,dy:4}); txt(c,pl,px+100,809,{size:27,w:500,al:'center'}); }); txt(c,'Runs natively on macOS and Linux*',W/2,882,{size:25,w:400,al:'center',col:C.ink}); },{dy:8,dur:1.4});
  line(c,'Open-source digital TV and radio receiver for your SDR.',W/2,950,bt,10.0,{size:26,w:300,col:C.dim,al:'center',dur:1.4});
  line(c,'github.com/adswill/OnAir',W/2,1000,bt,10.8,{size:28,w:500,col:PAL.accent,al:'center',dur:1.4});
  line(c,'* Windows coming soon.',W/2,1046,bt,11.4,{size:20,w:300,col:C.dim,al:'center',dur:1.4});
};

// ---------------------------------------------------------------- timeline (bar lines at 120 BPM: one bar = 2 s) and cue grid for music.py
const B=[['title',0,10,null,0],['standards',10,18,'dissolve',.7],['radios',18,24,'dissolve',.7],['tv',24,32,'cut',0],['radio',32,40,'dissolve',.7],['auto',40,46,'dissolve',.7],['see',46,54,'cut',0],['fix',54,64,'dissolve',.7],['exact',64,70,'dissolve',.7],['out',70,76,'dissolve',.7],['finale',76,84,'dissolve',.7],['outro',84,98,'dissolve',.9]];
// on-screen cues (seconds), all on the beat; music.py reads this list. Hits: 4 (title), 24 (live TV), 46 (signal), 80 (finale).
const CUES=[7.0,7.5,8.0,8.5,9.0,
  11.0,12.0,13.0,14.0,15.0,15.5,16.0,
  19.0,19.5,20.0,20.5,21.0,21.5,22.0,22.5,
  25.5,26.0,26.5,27.0,28.0,
  34.0,34.5,35.0,35.5,36.0,36.5,37.5,38.0,38.5,
  41.0,41.5,42.0,42.5,43.0,43.5,44.0,44.5,
  47.0,48.0,49.0,49.5,50.0,
  55.5,56.0,56.5,57.0,57.5,59.0,59.5,60.0,60.5,61.0,61.5,62.0,62.5,
  65.0,66.0,67.0,
  71.5,72.0,72.5,
  77.0,77.5,78.0,78.5,79.0,79.5,
  87.5,88.5,89.5,90.5,91.5,93.0];
const TAG={standards:'STANDARDS',radios:'ANY RADIO',tv:'LIVE TV',radio:'DIGITAL RADIO',auto:'AUTOMATIC',see:'SIGNAL',fix:'RECEPTION',exact:'EXACT',out:'OUTPUT'};
const SCENES=B.map((s,i)=>{ const next=B[i+1]; const t0=s[1]-(s[4]||0)/2; const t1=s[2]+(next?next[4]/2:0); const o=[s[0],t1-t0,s[3],s[4]||0]; o.t0=t0; o.t1=t1; o.start=s[1]; o.idx=i; return o; });
const TOTAL=98; window.TOTAL=TOTAL; window.NF=Math.round(TOTAL*FPS);
window.SCENES=SCENES.map(s=>({name:s[0],t0:s.t0,t1:s.t1,tr:s[2],trd:s[3]}));
const bufA=mk(), bufB=mk(), ca=bufA.getContext('2d'), cb=bufB.getContext('2d');
function sceneAt(c,s,t){ c.clearRect(0,0,W,H); const bt=t-s.start; CURBT=bt; CURT=t; c.save();
  const z=1+.0025*clamp(bt+.6,0,14); c.translate(W/2,H/2); c.scale(z,z); c.translate(-W/2,-H/2);
  T[s[0]](c,bt); c.restore();
  if(TAG[s[0]]){ const a=ease(bt,.6,.8); c.save(); c.globalAlpha=a*.7; txt(c,String(s.idx).padStart(2,'0')+'  ·  '+TAG[s[0]],W-110,84,{size:17,mono:true,w:500,col:C.dim,al:'right',ls:3}); c.restore(); } }
function composite(t){
  const act=SCENES.filter(s=>t>=s.t0&&t<s.t1);
  if(act.length===1){ sceneAt(ctx,act[0],t); return; }
  const A=act[0],Bn=act[1], p=clamp((t-Bn.t0)/Bn[3]); sceneAt(ca,A,t); sceneAt(cb,Bn,t);
  ctx.fillStyle='#000'; ctx.fillRect(0,0,W,H); ctx.drawImage(bufA,0,0); ctx.save(); ctx.globalAlpha=eio3(p); ctx.drawImage(bufB,0,0); ctx.restore();
}
// ---------------------------------------------------------------- post: faint bloom, vignette, fine grain, fades
const bl1=mk(480,270), bl1x=bl1.getContext('2d'), bl2=mk(240,135), bl2x=bl2.getContext('2d');
const grain=mk(256,256), gx_=grain.getContext('2d'); { const id=gx_.createImageData(256,256); for(let i=0;i<256*256;i++){ const v=(hash(i*1.37)*255)|0; id.data[i*4]=id.data[i*4+1]=id.data[i*4+2]=v; id.data[i*4+3]=255; } gx_.putImageData(id,0,0); }
const BUILDS=[[0,4],[20,24],[42,46],[72,80]];
function post(t,frame){
  // the picture builds with the music: a thin carrier line grows along the bottom edge during each riser and is released on the hit
  for(const [a,b] of BUILDS){ if(t>=a&&t<b+.25){ const k=t<b?Math.pow((t-a)/(b-a),2.2):1, rel=t<b?1:1-seg(t,b,b+.25); if(a>0||true){ ctx.save(); ctx.globalAlpha=.8*rel; ctx.strokeStyle='rgba(120,185,230,.9)'; ctx.lineWidth=2; ctx.beginPath(); ctx.moveTo(W/2-W*k/2,H-4); ctx.lineTo(W/2+W*k/2,H-4); ctx.stroke(); ctx.restore(); } } }
  const g=ctx.createRadialGradient(W/2,H/2,H*.5,W/2,H/2,H*1.02); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.28)'); ctx.fillStyle=g; ctx.fillRect(0,0,W,H);
  ctx.save(); ctx.globalAlpha=.028; ctx.globalCompositeOperation='overlay'; const ox=(hash(frame*1.7)*256)|0, oy=(hash(frame*2.3)*256)|0; for(let x=-ox;x<W;x+=256) for(let y=-oy;y<H;y+=256) ctx.drawImage(grain,x,y); ctx.restore();
  const o=seg(t,TOTAL-1.4,TOTAL); if(o>0){ctx.fillStyle=`rgba(0,0,0,${o})`;ctx.fillRect(0,0,W,H)}
  const f=1-seg(t,0,.8); if(f>0){ctx.fillStyle=`rgba(0,0,0,${f})`;ctx.fillRect(0,0,W,H)}
}
window.ready=Promise.all([document.fonts.load('300 20px Inter'),document.fonts.load('400 20px Inter'),document.fonts.load('500 20px Inter'),document.fonts.load('600 20px Inter'),document.fonts.load('500 20px "JetBrains Mono"'),logo.decode()]);
window.renderFrame=async(i)=>{ const t=i/FPS; composite(t); post(t,i); return t; };
