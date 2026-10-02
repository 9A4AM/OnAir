// OnAir promo v3: demonstrations drawn on a canvas, using the app's real palette (calm single accent colour, navy waterfall ramp,
// accent-coloured constellations on near-black plots). No imitation of the app's windows: just the signal, the way the app shows it.
// Needs common_head.js. Scene functions get bt = seconds since the scene's bar line (negative while it transitions in).
Object.assign(C,{blue:'#5ca8db',green:'#66c285',amber:'#e6b347',red:'#e06157',ink:'#e0e5eb',dim:'#7f8c99',dim2:'#4d565f'});
const PAL={accent:'#5ca8db',ok:'#66c285',warn:'#e6b347',bad:'#e06157',grey:'#9eabba',plot:'#030405',border:'#383f47',panel:'#141619'};
const acc=a=>`rgba(92,168,219,${a})`;
let CURBT=0; const bp=bt=>Math.exp(-7*(((bt%.5)+.5)%.5));      // 1 on every beat (120 BPM), decays to ~0 before the next

// ---- signal model (same shapes the receiver sees)
function floorDb(u,t){return -86+vnoise(u*90+t*9)*3+vnoise(u*23-t*2)*2}
function ofdm(u,t,c0,half,k=1){const e=sstep(c0-half-.012,c0-half+.012,u)*(1-sstep(c0+half-.012,c0+half+.012,u)); const rip=vnoise(u*140+t*14)*2.2+Math.sin(u*38+t*.7)*1.4; return -90+e*k*(43+rip);}
function specDb(u,t,sigs){let v=floorDb(u,t); for(const s of sigs) v=Math.max(v,ofdm(u,t,s.u,s.w,s.k??1)); return v;}
// waterfall: the app's navy -> deep blue -> teal -> pale ice ramp
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
  for(let i=0;i<=N;i++){const u=i/N, d=specDb(u,t,sigs); const yy=y+h-clamp((d-lo)/(hi-lo))*h; pts.push([x+u*w,yy]); const p=specDb(u,t-.35,sigs)+3; pk.push([x+u*w,y+h-clamp((Math.max(d,p)-lo)/(hi-lo))*h]);}
  for(const s of sigs){ const k=s.k??1; if(k>0.02){c.fillStyle=acc(.10*Math.min(1,k)); c.fillRect(x+(s.u-s.w)*w,y,2*s.w*w,h);} }
  c.strokeStyle='rgba(158,171,186,.38)'; c.lineWidth=1.4; c.beginPath(); pk.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke();
  c.strokeStyle=PAL.accent; c.lineWidth=o.lw||2.2; c.lineJoin='round'; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke();
  c.restore();
  if(o.axes){ for(let i=0;i<=8;i++) txt(c,(518+i)+'',x+(i+.5)*w/9.0*1.0+0,y+h+26,{size:o.fs||15,mono:true,col:C.dim,al:'center'}); }
}
function constel(c,x,y,s,M,sigma,t,o={}){
  plotFrame(c,x,y,s,s); const side=Math.sqrt(M), n=o.n||(M<=4?300:1400), cell=s/(side+.6); c.save(); c.beginPath(); c.rect(x+2,y+2,s-4,s-4); c.clip();
  const pos=i=>{ if(M===2) return [(i&1?1:-1)*.3*s,0]; if(M===4) return [(i&1?1:-1)*.2*s,((i>>1)&1?1:-1)*.2*s]; const a=i%side,b=Math.floor(i/side)%side; return [(a-(side-1)/2)*cell,(b-(side-1)/2)*cell]; };
  c.fillStyle=acc(o.a??.78); const ds=o.dot||Math.max(2,s/260);
  for(let k=0;k<n;k++){ const [px,py]=pos(Math.floor(hash(k*3.1+7)*M)); const ph=t*.6+k; const sg=sigma*cell;
    const gx=gauss(k)*sg+Math.sin(ph)*sg*.1, gy=gauss(k+9999)*sg+Math.cos(ph*1.3)*sg*.1; c.fillRect(x+s/2+px+gx-ds/2,y+s/2+py+gy-ds/2,ds,ds); }
  c.strokeStyle='rgba(255,255,255,.12)'; c.lineWidth=1; c.beginPath();
  if(M>4) for(let a=0;a<side;a++){const q=(a-(side-1)/2)*cell; c.moveTo(x+s/2+q,y+6);c.lineTo(x+s/2+q,y+s-6);c.moveTo(x+6,y+s/2+q);c.lineTo(x+s-6,y+s/2+q);}
  c.moveTo(x+s/2,y);c.lineTo(x+s/2,y+s);c.moveTo(x,y+s/2);c.lineTo(x+s,y+s/2);c.stroke(); c.restore();
}
function lamp(c,x,y,on,label,size=26){ c.fillStyle=on>0?PAL.ok:'#434a52'; c.beginPath(); c.arc(x,y,size*.3,0,7); c.fill(); if(on>0){c.save();c.globalAlpha=.35*on;c.shadowColor=PAL.ok;c.shadowBlur=16;c.fill();c.restore();} txt(c,label,x+size*.65,y+size*.35,{size,w:500,col:on>0?C.ink:C.dim}); }
function bgd(c,glow=.7){ c.fillStyle=C.bg; c.fillRect(0,0,W,H); const g=c.createRadialGradient(W*.7,H*.55,0,W*.7,H*.55,W*.7); g.addColorStop(0,`rgba(40,110,170,${(.13+.05*bp(CURBT))*glow})`); g.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g; c.fillRect(0,0,W,H);
  // broadcast rings: one new ring leaves the tower every two beats
  c.save(); c.lineWidth=2; for(let k=0;k<3;k++){ const u=((CURBT/2+k/3)%1+1)%1, r=60+u*1100; c.strokeStyle=`rgba(92,168,219,${.07*(1-u)*glow})`; c.beginPath(); c.arc(W*.86,H*.5,r,0,7); c.stroke(); } c.restore(); }
function head(c,l1,l2,t,t0,o={}){ const x=o.x??110,y=o.y??140,s=o.size??64; reveal(c,l1,x,y,t,t0,{size:s,w:500,ls:-s*.035,al:o.al||'left'}); if(l2) reveal(c,l2,x,y+s*1.08,t,t0+.25,{size:s,w:500,ls:-s*.035,col:o.col||PAL.accent,al:o.al||'left'}); }
function chip(c,s,cx,cy,a=1,o={}){ c.save(); c.globalAlpha*=a; c.font=`${o.w||500} ${o.size||24}px Inter`; const tw=c.measureText(s).width+(o.pad||44), th=(o.size||24)+26; rr(c,cx-tw/2,cy-th/2,tw,th,th/2); c.fillStyle=o.fill||'rgba(255,255,255,.04)'; c.fill(); c.strokeStyle=o.stroke||'rgba(255,255,255,.18)'; c.lineWidth=1.4; c.stroke(); txt(c,s,cx,cy+(o.size||24)*.35,{size:o.size||24,w:o.w||500,al:'center',col:o.col||C.ink}); c.restore(); return tw; }
const flash=(c,a)=>{ if(a>0){c.save();c.fillStyle=`rgba(255,255,255,${a})`;c.fillRect(0,0,W,H);c.restore();} };
function scan(c,a=.12){ c.save(); c.fillStyle=`rgba(0,0,0,${a})`; for(let y=0;y<H;y+=4) c.fillRect(0,y+2,W,2); c.restore(); }

// ---- a TV picture (colour bars, a bouncing ball, a station lower third): broadcast content, not app UI
function tvPicture(c,x,y,w,h,t,o={}){
  c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip();
  const bars=['#c4c4c4','#c4c400','#00c4c4','#00c400','#c400c4','#c40000','#0000c4'], bw=w/7;
  bars.forEach((b,i)=>{c.fillStyle=b;c.fillRect(x+i*bw,y,bw+1,h*.66)});
  ['#0000c4','#0a0a0a','#c400c4','#0a0a0a','#00c4c4','#0a0a0a','#c4c4c4'].forEach((b,i)=>{c.fillStyle=b;c.fillRect(x+i*bw,y+h*.66,bw+1,h*.08)});
  c.fillStyle='#0b0e14'; c.fillRect(x,y+h*.74,w,h*.26); c.fillStyle='#f2f2f2'; c.fillRect(x+bw*1.1,y+h*.74,bw*1.5,h*.26); c.fillStyle='#2a0f55'; c.fillRect(x+bw*2.7,y+h*.74,bw*1.5,h*.26);
  // bouncing ball (proves it is moving picture)
  const bx=x+w*(.5+.42*Math.sin(t*1.7)), by=y+h*(.33+.27*Math.abs(Math.sin(t*2.3))); c.fillStyle='#fff'; c.beginPath(); c.arc(bx,by,h*.055,0,7); c.fill(); c.fillStyle='rgba(0,0,0,.25)'; c.beginPath(); c.arc(bx+h*.012,by+h*.012,h*.055,0,7); c.fill();
  const lt=eo5(seg(t,o.lt??.6,(o.lt??.6)+.9)); if(!o.noLower){ c.save(); c.translate(-(1-lt)*w*.6,0); rr(c,x+w*.04,y+h*.8,w*.46,h*.14,h*.012); c.fillStyle='rgba(8,12,18,.9)'; c.fill(); c.fillStyle=PAL.bad; c.fillRect(x+w*.04,y+h*.8,h*.012,h*.14);
    txt(c,'Channel One HD',x+w*.04+h*.04,y+h*.8+h*.06,{size:h*.047,w:600}); txt(c,'Evening News  18:00',x+w*.04+h*.04,y+h*.8+h*.11,{size:h*.032,col:'#b9c4cf',w:400}); c.restore(); }
  rr(c,x+w*.84,y+h*.05,w*.12,h*.075,h*.012); c.fillStyle=PAL.bad; c.fill(); txt(c,'ON AIR',x+w*.9,y+h*.05+h*.052,{size:h*.04,w:700,al:'center',col:'#fff'});
  c.fillStyle='rgba(0,0,0,.14)'; for(let yy=0;yy<h;yy+=4) c.fillRect(x,y+yy+2,w,2);
  c.restore();
}

const T={};
// ---------------------------------------------------------------- hook: Tune. Lock. Watch.
T.hook=(c,bt)=>{
  c.fillStyle='#000'; c.fillRect(0,0,W,H);
  const punch=t0=>1.12-.12*eo5(seg(bt,t0,t0+.55));
  if(bt<2.0){ const s=punch(0); c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
    const k=eo3(seg(bt,.15,1.5)), sigs=[{u:.5,w:.3,k}];
    spectrum(c,80,130,1760,430,bt+8,sigs,{n:520,lw:3,axes:true,fs:17}); waterfall(c,80,610,1760,330,bt+8,sigs);
    c.strokeStyle=PAL.border; c.lineWidth=1.5; c.strokeRect(80.75,610.75,1758.5,328.5);
    c.restore();
    const g=c.createLinearGradient(0,H*.55,0,H); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.7)'); c.fillStyle=g; c.fillRect(0,H*.55,W,H*.45);
    const f=lerp(517.6,522.0,eo3(seg(bt,.1,1.5))); txt(c,f.toFixed(3)+' MHz',1840,92,{size:56,w:500,mono:true,al:'right'});
    reveal(c,'Tune.',100,1010,bt,.02,{size:200,w:700,ls:-9}); }
  else if(bt<4.0){ const tt=bt-2, s=punch(2); c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
    c.fillStyle='#050607'; c.fillRect(0,0,W,H);
    const sg=lerp(.62,.045,eo3(seg(tt,.35,1.55))); constel(c,1000,150,780,64,sg,bt,{n:2600,dot:4.2}); c.restore();
    reveal(c,'Lock.',100,1010,bt,2.02,{size:200,w:700,ls:-9});
    const mer=lerp(8.2,31.4,eo3(seg(tt,.35,1.55))); txt(c,'MER',110,170,{size:26,w:500,col:C.dim}); txt(c,mer.toFixed(1)+' dB',110,236,{size:64,w:500,mono:true,col:PAL.accent});
    ['IQ','P1','L1-pre','LDPC'].forEach((n,i)=>lamp(c,128,330+i*58,eo3(seg(bt,2.5+i*.25,2.75+i*.25)),n,30)); }
  else { const tt=bt-4, s=punch(4); c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
    tvPicture(c,0,0,W,H,bt,{lt:4.5-4+.0}); c.restore();
    const g=c.createLinearGradient(0,H*.5,0,H); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.8)'); c.fillStyle=g; c.fillRect(0,H*.5,W,H*.5);
    reveal(c,'Watch.',100,1010,bt,4.02,{size:200,w:700,ls:-9}); }
  flash(c,.5*(1-seg(bt,0,.12))); flash(c,(bt>=2?.45:0)*(1-seg(bt,2,2.14))); flash(c,(bt>=4?.45:0)*(1-seg(bt,4,4.14)));
};
T.logo=(c,bt)=>{
  bgd(c,seg(bt,0,1));
  const sigs=[{u:.5,w:.3,k:1}]; c.save(); c.globalAlpha=.55*eo3(seg(bt,.5,2)); spectrum(c,0,860,W,200,bt+3,sigs,{n:500,lw:2,noFrame:true,grid:false}); c.restore();
  c.fillStyle=C.bg; c.globalAlpha=.0; c.fillRect(0,0,0,0); c.globalAlpha=1;
  const lk=eoBack(seg(bt,.1,.9))*(1+.025*bp(bt)*seg(bt,1,1.5)); c.save(); c.translate(W/2,350); c.scale(lk,lk);
  const gl=c.createRadialGradient(0,0,0,0,0,380); gl.addColorStop(0,'rgba(224,97,87,.20)'); gl.addColorStop(1,'rgba(224,97,87,0)'); c.fillStyle=gl; c.fillRect(-420,-420,840,840);
  c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=50; c.shadowOffsetY=20; c.globalAlpha=seg(bt,.1,.4); c.drawImage(logo,-170,-170,340,340); c.restore();
  reveal(c,'OnAir',W/2,690,bt,.9,{size:128,w:600,ls:-5,al:'center'});
  reveal(c,'Digital TV, decoded in software.',W/2,770,bt,1.5,{size:46,w:400,col:C.ink,ls:-1,al:'center'});
  ['DVB-T2','DVB-T','ATSC'].forEach((s,i)=>chip(c,s,W/2+(i-1)*210,840,eo5(seg(bt,2.4+i*.15,3.0+i*.15)),{size:26}));
};
// ---------------------------------------------------------------- any radio
const RADIOS=[['HackRF One / Pro','native driver'],['Airspy','via SoapySDR'],['SDRplay RSP','via SoapySDR'],['PlutoSDR','via SoapySDR'],['BladeRF','via SoapySDR'],['LimeSDR','via SoapySDR'],['USRP','via SoapySDR'],['RTL-SDR','narrow signals only']];
T.radios=(c,bt)=>{
  bgd(c,.9);
  head(c,'Bring your own radio.',null,bt,.2,{size:66,y:150});
  reveal(c,'HackRF natively. Everything else through SoapySDR.',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  const lx=110, ly=300, cw=470, ch=96, gx=26, gy=22, logoC=[1530,560];
  // wires first
  RADIOS.forEach((r,i)=>{ const col=i%2, row=Math.floor(i/2); const x=lx+col*(cw+gx), y=ly+row*(ch+gy); const a=eo5(seg(bt,1.0+i*.5,1.6+i*.5)); if(a<=0) return;
    const x1=x+cw, y1=y+ch/2; c.save(); c.globalAlpha=a*.55; c.strokeStyle=PAL.accent; c.lineWidth=1.8; c.beginPath(); c.moveTo(x1,y1); c.bezierCurveTo(x1+260,y1,logoC[0]-300,logoC[1],logoC[0]-150,logoC[1]); c.stroke(); c.restore();
    for(let p=0;p<4;p++){ const u=((bt*.35+p/4+i*.17)%1); const bx=bez(x1,x1+260,logoC[0]-300,logoC[0]-150,u), by=bez(y1,y1,logoC[1],logoC[1],u); c.save(); c.globalAlpha=a*Math.sin(u*Math.PI)*.9; c.fillStyle=PAL.accent; c.beginPath(); c.arc(bx,by,4.5,0,7); c.fill(); c.restore(); } });
  RADIOS.forEach((r,i)=>{ const col=i%2, row=Math.floor(i/2); const x=lx+col*(cw+gx), y=ly+row*(ch+gy); const a=eo5(seg(bt,1.0+i*.5,1.6+i*.5)); if(a<=0) return;
    c.save(); c.globalAlpha=a; c.translate((1-a)*-40,0); rr(c,x,y,cw,ch,16); c.fillStyle='#0d1013'; c.fill(); c.strokeStyle=i===0?acc(.7):'rgba(255,255,255,.12)'; c.lineWidth=i===0?2:1.4; c.stroke();
    txt(c,r[0],x+28,y+42,{size:30,w:600,ls:-.4}); txt(c,r[1],x+28,y+74,{size:20,col:i===0?PAL.accent:C.dim,w:400}); c.restore(); });
  const lk=eo5(seg(bt,.6,1.6)); c.save(); c.translate(logoC[0],logoC[1]); c.scale(lk,lk); const gl=c.createRadialGradient(0,0,0,0,0,300); gl.addColorStop(0,'rgba(92,168,219,.18)'); gl.addColorStop(1,'rgba(92,168,219,0)'); c.fillStyle=gl; c.fillRect(-320,-320,640,640);
  c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=40; c.shadowOffsetY=16; c.globalAlpha=lk; c.drawImage(logo,-130,-130,260,260); c.restore();
  reveal(c,'OnAir',logoC[0],logoC[1]+205,bt,1.2,{size:44,w:600,al:'center'});
  reveal(c,'A radio needs about 1 Msps per MHz of channel width.',110,1000,bt,5.2,{size:24,w:400,col:C.dim2==='x'?C.dim:C.dim});
};
function bez(a,b,c2,d,t){const u=1-t;return u*u*u*a+3*u*u*t*b+3*u*t*t*c2+t*t*t*d}
// ---------------------------------------------------------------- auto detection + scanner
T.auto=(c,bt)=>{
  bgd(c,.9);
  head(c,'It finds the signal.','You don’t configure a thing.',bt,.2,{size:60,y:140});
  const cards=[['DVB-T2','T2-Base and T2-Lite','1.7 – 10 MHz · up to 256-QAM',.34,'ofdm'],['DVB-T','2K and 8K','QPSK – 64-QAM · all guard intervals',.26,'ofdm'],['ATSC 1.0','8-VSB','6 MHz · decision-feedback equaliser',.30,'vsb']];
  const sel=bt<2.5?-1:Math.min(2,Math.floor((bt-2.5)/.5));
  cards.forEach((cd,i)=>{ const x=110+i*590, y=300, w=550, h=330, k=eo5(seg(bt,.7+i*.15,1.7+i*.15)); if(k<=0) return;
    c.save(); c.globalAlpha=k; c.translate(0,(1-k)*40); const on=sel===i; rr(c,x,y,w,h,20); c.fillStyle='#0b0e11'; c.fill(); c.strokeStyle=on?acc(.85):'#262b31'; c.lineWidth=on?2.4:1.3; c.stroke();
    txt(c,cd[0],x+30,y+66,{size:48,w:600,ls:-1.5}); txt(c,cd[1],x+30,y+102,{size:22,w:400}); txt(c,cd[2],x+30,y+132,{size:18,col:C.dim});
    const gx=x+30, gy=y+158, gw=w-60, gh=110; plotFrame(c,gx,gy,gw,gh); c.save(); c.beginPath(); c.rect(gx,gy,gw,gh); c.clip(); const pts=[];
    for(let j=0;j<=200;j++){ const u=j/200; const e=sstep(.5-cd[3]-.02,.5-cd[3]+.02,u)*(1-sstep(.5+cd[3]-.02,.5+cd[3]+.02,u)); let v=.1+e*(.52+vnoise(u*120+bt*12)*.05)+vnoise(u*50+bt*3)*.02; if(cd[4]==='vsb') v+=Math.exp(-Math.pow((u-(.5-cd[3]+.012))/.008,2))*.3; pts.push([gx+u*gw,gy+gh-clamp(v)*gh*1.2]); }
    c.strokeStyle=on?PAL.accent:'rgba(92,168,219,.5)'; c.lineWidth=2.2; c.beginPath(); pts.forEach((p,j)=>j?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke(); c.restore();
    const ok=on||(sel>i); txt(c,ok?'●  detected':'○  looking',x+30,y+h-18,{size:18,w:500,col:ok?PAL.ok:C.dim2,mono:true}); c.restore(); });
  // scanner sweep
  const k=eo5(seg(bt,3.8,4.6)); if(k>0){ c.save(); c.globalAlpha=k; const x=110,y=740,w=1700,h=220; txt(c,'Channel scanner',x,y-64,{size:28,w:600,col:C.ink}); txt(c,'470 – 694 MHz',x+w,y-64,{size:20,mono:true,col:C.dim,al:'right'});
    const muxes=[[.12,'474'],[.38,'522'],[.62,'570'],[.86,'618']]; const sw=seg(bt,4.4,6.8);
    const sigs=muxes.filter((m,i)=>sw>(m[0]-.02)).map(m=>({u:m[0],w:.035,k:1}));
    // always draw the floor + the muxes the sweep has passed
    plotFrame(c,x,y,w,h); c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip(); const pts=[]; for(let i=0;i<=500;i++){ const u=i/500; let d=floorDb(u,bt); for(const m of muxes){ if(sw>m[0]-.01) d=Math.max(d,ofdm(u,bt,m[0],.035)); } pts.push([x+u*w,y+h-clamp((d+100)/68)*h]); }
    c.strokeStyle=PAL.accent; c.lineWidth=2.2; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke();
    c.fillStyle='rgba(92,168,219,.16)'; c.fillRect(x+w*Math.max(0,sw-.05),y,w*.05*(sw<1?1:0),h); c.strokeStyle='rgba(255,255,255,.5)'; c.lineWidth=2; c.beginPath(); c.moveTo(x+w*sw,y); c.lineTo(x+w*sw,y+h); c.stroke(); c.restore();
    muxes.forEach(([u,l],i)=>{ const a=eo5(seg(bt,5+i*.5,5.5+i*.5)); if(a<=0) return; c.save(); c.globalAlpha=a; const px=x+u*w; c.strokeStyle=PAL.ok; c.lineWidth=2; c.beginPath(); c.moveTo(px,y+h*.2); c.lineTo(px,y-6); c.stroke(); txt(c,l+' MHz',px,y-14,{size:20,w:600,mono:true,al:'center',col:PAL.ok}); c.restore(); });
    c.restore(); }
};
// ---------------------------------------------------------------- live TV
T.live=(c,bt)=>{
  bgd(c,.9);
  head(c,'Watch live TV.',null,bt,.2,{size:66,y:150});
  reveal(c,'Video, audio, subtitles, teletext and a programme guide.',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  const k=eo5(seg(bt,.4,1.5)); c.save(); c.globalAlpha=k; c.translate(0,(1-k)*50);
  const x=110,y=290,w=1060,h=596; c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=60; c.shadowOffsetY=24; rr(c,x-18,y-18,w+36,h+36,34); c.fillStyle='#14181c'; c.fill(); c.shadowColor='transparent'; c.strokeStyle='rgba(255,255,255,.12)'; c.lineWidth=1.5; c.stroke();
  c.save(); rr(c,x,y,w,h,18); c.clip(); tvPicture(c,x,y,w,h,bt,{lt:.9});
  const sb=eo5(seg(bt,2.5,3.1)); if(sb>0){ c.globalAlpha=sb; rr(c,x+w*.18,y+h*.64,w*.64,h*.075,8); c.fillStyle='rgba(0,0,0,.78)'; c.fill(); txt(c,'…and now, the evening news.',x+w*.5,y+h*.64+h*.052,{size:h*.04,w:500,al:'center',col:'#fff'}); }
  c.restore(); c.restore();
  // teletext page
  const tk=eo5(seg(bt,1.2,2.2)); c.save(); c.globalAlpha=tk; c.translate((1-tk)*60,0);
  const tx=1280,ty=290,tw=530,th=596; rr(c,tx-14,ty-14,tw+28,th+28,26); c.fillStyle='#14181c'; c.fill(); c.strokeStyle='rgba(255,255,255,.12)'; c.stroke(); c.save(); rr(c,tx,ty,tw,th,12); c.clip(); c.fillStyle='#000'; c.fillRect(tx,ty,tw,th);
  const row=(n,s,col,bgc)=>{ const yy=ty+22+n*27; if(bgc){c.fillStyle=bgc;c.fillRect(tx+14,yy-21,tw-28,27);} txt(c,s,tx+22,yy,{size:21,mono:true,w:500,col}); };
  row(0,'P100  OnAir Text   18:04',  '#fff','#0000c8'); row(2,'NEWS','#ffeb3b'); row(3,'101 Headlines','#00e5ff'); row(4,'110 Politics','#00e5ff'); row(5,'120 World','#00e5ff'); row(7,'SPORT','#ffeb3b'); row(8,'200 Scores','#00e5ff'); row(9,'210 Fixtures','#00e5ff'); row(11,'WEATHER','#ffeb3b'); row(12,'300 Today','#00e5ff'); row(13,'310 Week ahead','#00e5ff');
  [['#e53935','NEWS'],['#43a047','SPORT'],['#fdd835','WEATHER'],['#00acc1','TV']].forEach(([cc,l],i)=>{ c.fillStyle=cc; c.fillRect(tx+14+i*(tw-28)/4,ty+th-60,(tw-28)/4-6,34); txt(c,l,tx+14+i*(tw-28)/4+((tw-28)/4-6)/2,ty+th-36,{size:17,mono:true,w:700,al:'center',col:'#000'}); });
  c.restore(); c.restore();
  // chips (on the grid of the music) and the quality meter
  [['Live video + audio',-1],['Programme guide',-1],['DVB subtitles',-1],['Teletext',-1]].forEach((cc,i)=>{ chip(c,cc[0],[310,720,1130,1480][i],975,eo5(seg(bt,1.5+i*.5,2.1+i*.5)),{size:24}); });
  const q=eo5(seg(bt,3.4,4.2)); if(q>0){ c.save(); c.globalAlpha=q; txt(c,'Signal quality',1280,250,{size:20,col:C.dim}); rr(c,1280,262,530,18,9); c.fillStyle='#14181c'; c.fill(); rr(c,1280,262,530*eo3(seg(bt,3.5,5.0)),18,9); c.fillStyle=PAL.ok; c.fill(); txt(c,'100% excellent',1810,250,{size:20,w:600,mono:true,al:'right',col:PAL.ok}); c.restore(); }
};
// ---------------------------------------------------------------- see everything
T.see=(c,bt)=>{
  bgd(c,.9);
  head(c,'See what the radio sees.',null,bt,.2,{size:66,y:150});
  reveal(c,'Every stage of the receiver is visible, live.',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  const sigs=[{u:.5,w:.3,k:1}];
  const a1=eo5(seg(bt,1.2,2.0)); c.save(); c.globalAlpha=a1; c.translate(0,(1-a1)*40); spectrum(c,110,290,1090,260,bt+5,sigs,{n:380,axes:true,fs:14}); waterfall(c,110,590,1090,250,bt+5,sigs); c.strokeStyle=PAL.border; c.lineWidth=1.5; c.strokeRect(110.75,590.75,1088.5,248.5); txt(c,'Spectrum and waterfall',110,278,{size:20,col:C.dim}); c.restore();
  const a2=eo5(seg(bt,1.5,2.5)); c.save(); c.globalAlpha=a2; c.translate(0,(1-a2)*40); const sg=lerp(.6,.05,eo3(seg(bt,2.2,6))); constel(c,1290,290,520,64,sg,bt,{n:1800,dot:3.2}); txt(c,'Constellation',1290,278,{size:20,col:C.dim});
  txt(c,'MER',1290,880,{size:20,col:C.dim}); txt(c,lerp(8.5,31.2,eo3(seg(bt,2.2,6))).toFixed(1)+' dB',1340,880,{size:30,w:500,mono:true,col:PAL.accent}); c.restore();
  // mini plots: channel response, SNR per carrier, FEC blocks
  const minis=[['Channel response',110],['SNR per carrier',500],['FEC blocks',890]]; minis.forEach(([nme,mx],i)=>{ const a=eo5(seg(bt,2.5+i*1.0,3.3+i*1.0)); if(a<=0) return; c.save(); c.globalAlpha=a; c.translate(0,(1-a)*30);
    const w=360,h=120,y=900; txt(c,nme,mx,y-12,{size:18,col:C.dim}); plotFrame(c,mx,y,w,h);
    if(i<2){ c.save(); c.beginPath(); c.rect(mx,y,w,h); c.clip(); c.strokeStyle=i? PAL.accent : PAL.accent; c.lineWidth=1.8; c.beginPath(); for(let j=0;j<=160;j++){ const u=j/160; const v=i===0? .55+.12*Math.sin(u*14+bt)+vnoise(u*30)*.06 : .62+vnoise(u*60+bt*3)*.18; const px=mx+u*w, py=y+h-clamp(v)*h; j?c.lineTo(px,py):c.moveTo(px,py);} c.stroke(); c.restore(); }
    else { for(let r=0;r<6;r++)for(let q=0;q<20;q++){ const bad=hash(r*31+q*7+3)>.97; c.fillStyle=bad?PAL.bad:PAL.ok; c.globalAlpha=a*.85; c.fillRect(mx+8+q*17.2,y+8+r*17.2,13,13); } }
    c.restore(); });
};
// ---------------------------------------------------------------- fix reception
T.fix=(c,bt)=>{
  bgd(c,.9);
  const sw=eio(seg(bt,4.5,5.3));
  c.save(); c.globalAlpha=1-sw; head(c,'Know what’s wrong.',null,bt,.2,{size:66,y:150}); reveal(c,'Echoes, fades and drop-outs, found and explained.',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  // impulse response
  const x=110,y=300,w=1520,h=560; plotFrame(c,x,y,w,h); c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip();
  c.strokeStyle='rgba(153,163,179,.16)'; c.lineWidth=1; for(let i=1;i<12;i++){c.beginPath();c.moveTo(x+i*w/12,y);c.lineTo(x+i*w/12,y+h);c.stroke()}
  const ux=u=>x+100+(u+1)*(w-160)/8; const gk=eo3(seg(bt,.8,1.6)); c.fillStyle='rgba(102,194,133,.08)'; c.fillRect(ux(0),y,(ux(7.0)-ux(0))*gk,h); c.strokeStyle='rgba(102,194,133,.5)'; c.setLineDash([6,6]); c.beginPath(); const gx=ux(0)+(ux(7.0)-ux(0))*gk; c.moveTo(gx,y); c.lineTo(gx,y+h); c.stroke(); c.setLineDash([]);
  txt(c,'guard interval',ux(0)+14,y+34,{size:18,col:PAL.ok,a:gk});
  const echoes=[[0,0],[1.1,-7],[2.3,-12],[4.4,-16],[5.8,-20]]; const base=y+h-46, scl=h-110;
  c.strokeStyle='#2c333a'; c.beginPath(); c.moveTo(x+40,base); c.lineTo(x+w-40,base); c.stroke(); for(let i=-1;i<=7;i++) txt(c,i+' µs',ux(i),base+30,{size:15,mono:true,col:C.dim,al:'center'});
  const pts=[]; for(let px=x+40;px<=x+w-40;px+=3){ let v=0; echoes.forEach(([d0,db],i)=>{ const kk=eo5(seg(bt,1.5+i*.5,2.1+i*.5)); const a=Math.pow(10,db/20)*kk; const dd=(px-ux(d0))/10; v+=a*Math.exp(-dd*dd); }); v+=Math.abs(vnoise(px*.07+bt*6))*.015; pts.push([px,base-v*scl]); }
  c.strokeStyle=PAL.accent; c.lineWidth=2.6; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke(); c.restore();
  echoes.forEach(([d0,db],i)=>{ const kk=eo5(seg(bt,1.5+i*.5,2.1+i*.5)); if(i>0&&kk>.5) txt(c,db+' dB',ux(d0),base-Math.pow(10,db/20)*scl-18,{size:20,w:600,mono:true,al:'center',col:i<3?PAL.warn:PAL.bad,a:kk}); });
  const b=eo5(seg(bt,3.3,3.9)); if(b>0){ c.save(); c.globalAlpha=b; c.translate(0,(1-b)*20); rr(c,1250,330,400,94,14); c.fillStyle='rgba(230,179,71,.12)'; c.fill(); c.strokeStyle=PAL.warn; c.lineWidth=2; c.stroke(); txt(c,'multipath: likely',1278,372,{size:32,w:600,col:PAL.warn}); txt(c,'echo −7 dB at +1.1 µs',1278,406,{size:19,mono:true,col:C.dim}); c.restore(); }
  c.restore();
  // antenna finder
  c.save(); c.globalAlpha=sw; c.translate((1-sw)*80,0);
  reveal(c,'…and where to point',110,430,bt,5.0,{size:70,w:500,ls:-2.5}); reveal(c,'the antenna.',110,515,bt,5.25,{size:70,w:500,ls:-2.5,col:PAL.accent}); reveal(c,'It measures every direction and tells you.',110,580,bt,5.7,{size:30,w:400,col:C.dim,ls:-.4});
  const cx=1390, cy=540, R=330; c.strokeStyle='#262b31'; c.lineWidth=1.6; for(const r of [R,R*.66,R*.33]){ c.beginPath(); c.arc(cx,cy,r,0,7); c.stroke(); }
  const dirs=['N','NE','E','SE','S','SW','W','NW'], score=[.38,.86,.52,.28,.22,.30,.41,.63];
  dirs.forEach((dn,i)=>{ const a=-Math.PI/2+i*Math.PI/4, rch=seg(bt,5.0+i*.5,5.6+i*.5), best=i===1&&bt>8.5; const len=R*score[i]*eo5(rch);
    c.strokeStyle=best?PAL.ok:PAL.accent; c.lineWidth=best?18:13; c.lineCap='round'; c.beginPath(); c.moveTo(cx+Math.cos(a)*44,cy+Math.sin(a)*44); c.lineTo(cx+Math.cos(a)*(44+len*(R-44)/R),cy+Math.sin(a)*(44+len*(R-44)/R)); c.stroke();
    txt(c,dn,cx+Math.cos(a)*(R+36),cy+Math.sin(a)*(R+36)+9,{size:24,w:600,al:'center',col:best?PAL.ok:C.dim});
    if(rch>.9) txt(c,Math.round(score[i]*100)+'',cx+Math.cos(a)*(44+len*(R-44)/R+26),cy+Math.sin(a)*(44+len*(R-44)/R+26)+6,{size:17,mono:true,al:'center',col:best?PAL.ok:C.ink}); });
  c.fillStyle='#141a20'; c.beginPath(); c.arc(cx,cy,40,0,7); c.fill(); c.strokeStyle='#39424a'; c.stroke();
  const fin=eo5(seg(bt,8.5,9.2)); if(fin>0){ c.save(); c.globalAlpha=fin; rr(c,110,640,560,86,16); c.fillStyle='rgba(102,194,133,.12)'; c.fill(); c.strokeStyle=PAL.ok; c.lineWidth=2; c.stroke(); txt(c,'Point it north-east.',140,696,{size:38,w:600,col:PAL.ok}); c.restore(); }
  else { const bar=seg(bt,5.0,8.5); txt(c,'Turn to '+dirs[Math.min(7,Math.floor(bar*8))]+' and hold still…',110,700,{size:26,mono:true,col:C.dim}); rr(c,110,722,560,8,4); c.fillStyle='#14181c'; c.fill(); rr(c,110,722,560*bar,8,4); c.fillStyle=PAL.accent; c.fill(); }
  c.restore();
};
// ---------------------------------------------------------------- record / stream
T.out=(c,bt)=>{
  bgd(c,.9);
  head(c,'Record it. Stream it.','Send it anywhere.',bt,.2,{size:66,y:150});
  const k=eo5(seg(bt,.6,1.6)); c.save(); c.globalAlpha=k;
  const node=(x,y,w,h,title,sub,col)=>{ rr(c,x,y,w,h,20); c.fillStyle='#0b0e11'; c.fill(); c.strokeStyle=col||'#2a2f35'; c.lineWidth=1.8; c.stroke(); txt(c,title,x+w/2,y+h/2-2,{size:34,w:600,al:'center'}); txt(c,sub,x+w/2,y+h/2+32,{size:19,col:C.dim,al:'center',mono:true}); };
  node(130,520,400,170,'OnAir','transport stream',acc(.85));
  const outs=[['.ts file','full mux or selected services',420],['UDP','unicast or multicast',605],['Live player','decoded in the app',790]]; const ox=1100;
  outs.forEach(([a,b,yy],i)=>{ const a2=eo5(seg(bt,1.5+i*.5,2.2+i*.5)); c.save(); c.globalAlpha=k*a2; node(ox,yy-60,450,126,a,b); c.restore();
    c.strokeStyle=acc(.4*a2); c.lineWidth=2; c.beginPath(); c.moveTo(530,605); c.bezierCurveTo(800,605,820,yy+3,ox,yy+3); c.stroke();
    for(let p=0;p<6;p++){ const u=((bt*.5+p/6+i*.13)%1); const xx=bez(530,800,820,ox,u), yy2=bez(605,605,yy+3,yy+3,u); c.fillStyle=PAL.accent; c.globalAlpha=k*a2*Math.sin(u*Math.PI); c.beginPath(); c.arc(xx,yy2,5.5,0,7); c.fill(); c.globalAlpha=k; } });
  ['VLC','ffplay','tvheadend'].forEach((n,i)=>chip(c,n,1700,545+i*60,eo5(seg(bt,3.0+i*.3,3.6+i*.3))*k,{size:21,pad:36}));
  c.restore();
};
// ---------------------------------------------------------------- T2-Lite
T.lite=(c,bt)=>{
  bgd(c,.9);
  head(c,'T2-Lite, too.',null,bt,.2,{size:66,y:150});
  reveal(c,'The low-power DVB-T2 profile that public decoders rarely cover.*',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  const a1=eo5(seg(bt,.8,1.6)); c.save(); c.globalAlpha=a1; c.translate(0,(1-a1)*40);
  const x=110,y=300,w=1100,h=380; const lite=eo5(seg(bt,1.5,2.4));
  plotFrame(c,x,y,w,h); c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip(); c.strokeStyle='rgba(153,163,179,.14)'; c.lineWidth=1; for(let i=1;i<10;i++){c.beginPath();c.moveTo(x+i*w/10,y);c.lineTo(x+i*w/10,y+h);c.stroke()}
  const sigsL=[{u:.3,w:.22,k:1},{u:.82,w:.045,k:lite}], pts=[];
  for(let i=0;i<=520;i++){ const u=i/520, d=specDb(u,bt+2,sigsL); pts.push([x+u*w,y+h-clamp((d+100)/68)*h]); }
  c.fillStyle='rgba(158,171,186,.07)'; c.fillRect(x+.08*w,y,.44*w,h); c.fillStyle=acc(.16*lite+.001); c.fillRect(x+.775*w,y,.09*w,h);
  c.strokeStyle=PAL.accent; c.lineWidth=2.6; c.lineJoin='round'; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke(); c.restore();
  txt(c,'8 MHz  T2-Base',x+.3*w,y+h-14,{size:20,col:C.dim,al:'center'});
  if(lite>0){ c.save(); c.globalAlpha=lite; const lx=x+.82*w; c.strokeStyle=PAL.accent; c.lineWidth=2; c.beginPath(); c.moveTo(lx,y+h*.28); c.lineTo(lx,y+34); c.stroke(); txt(c,'1.7 MHz  T2-Lite',lx,y+24,{size:26,w:600,col:PAL.accent,al:'center'}); c.restore(); }
  c.restore();
  // S1 readout, as the receiver reports it
  const rk=eo5(seg(bt,2.5,3.1)); if(rk>0){ c.save(); c.globalAlpha=rk; rr(c,110,720,1100,70,14); c.fillStyle='#0b0e11'; c.fill(); c.strokeStyle='rgba(255,255,255,.1)'; c.lineWidth=1.3; c.stroke();
    lamp(c,150,757,1,'',30); txt(c,'S1',186,764,{size:24,mono:true,col:C.dim}); txt(c,'T2-Lite SISO',250,764,{size:28,mono:true,w:500,col:PAL.accent}); txt(c,'detected from the preamble',640,764,{size:22,col:C.dim}); c.restore(); }
  // an RTL-SDR-sized window: 1.7 MHz fits, 8 MHz does not
  const kk=eo5(seg(bt,1.2,2.0)); c.save(); c.globalAlpha=kk; c.translate((1-kk)*60,0);
  const gx=1300, gw=510; txt(c,'A cheap RTL-SDR sees about 2.4 MHz',gx,330,{size:24,w:600});
  rr(c,gx,360,gw,64,12); c.fillStyle='#0b0e11'; c.fill(); c.strokeStyle='rgba(255,255,255,.14)'; c.lineWidth=1.4; c.stroke();
  const pw=gw*1.7/2.4, p1=eo5(seg(bt,3.0,3.8)); rr(c,gx+(gw-pw)/2,370,pw*p1,44,8); c.fillStyle=acc(.35); c.fill(); c.strokeStyle=PAL.accent; c.lineWidth=2; c.stroke();
  txt(c,'T2-Lite fits',gx+gw/2,399,{size:22,w:600,al:'center',col:C.ink,a:p1});
  const p2=eo5(seg(bt,4.0,4.8)); c.save(); c.globalAlpha=kk*p2;
  const grd=c.createLinearGradient(gx,0,gx+gw+110,0); grd.addColorStop(0,'rgba(230,179,71,.30)'); grd.addColorStop(.8,'rgba(230,179,71,.18)'); grd.addColorStop(1,'rgba(230,179,71,0)');
  rr(c,gx,470,gw+110,52,10); c.fillStyle=grd; c.fill(); c.strokeStyle='rgba(230,179,71,.7)'; c.lineWidth=2; c.stroke();
  txt(c,'8 MHz  T2-Base',gx+22,505,{size:22,w:600,col:PAL.warn}); txt(c,'\u203a\u203a\u203a',gx+gw+30,507,{size:30,w:700,col:PAL.warn});
  txt(c,'wider than the radio can see',gx,556,{size:21,w:500,col:PAL.warn}); c.restore(); c.restore();
  // chips on the beat grid
  ['FEC rates 1/3 and 2/5','Auto-detected from S1','Same engine as T2-Base'].forEach((s,i)=>chip(c,s,[360,820,1330][i],850,eo5(seg(bt,4.0+i*.5,4.6+i*.5)),{size:24}));
  reveal(c,'* To our knowledge. Locks in synthetic tests, FEC rates checked against the reference encoder; no real T2-Lite signal tried yet.',110,1010,bt,5.5,{size:18,w:400,col:C.dim2==='x'?C.dim:'#6c7782'});
};
// ---------------------------------------------------------------- built to be exact
T.exact=(c,bt)=>{
  bgd(c,.9);
  head(c,'Built to be exact.',null,bt,.2,{size:66,y:150});
  reveal(c,'A receiver you can trust, because it is checked against the reference.',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  const cards=[['Bit-exact','Coding chains checked against GNU Radio’s reference encoders.'],['Live-tested','Locked on a real DVB-T2 multiplex at 522 MHz.'],['Real-time','LDPC on the GPU with Metal, or SIMD on the CPU.']];
  cards.forEach((cd,i)=>{ const x=110+i*600, y=310, w=560, h=560, k=eo5(seg(bt,1.0+i*1.0,1.8+i*1.0)); if(k<=0) return; const pu=1+.012*bp(bt-1-i)*(bt>1+i?1:0);
    c.save(); c.globalAlpha=k; c.translate(x+w/2,y+h/2+(1-k)*50); c.scale(pu,pu); c.translate(-(x+w/2),-(y+h/2));
    rr(c,x,y,w,h,24); c.fillStyle='#0b0e11'; c.fill(); c.strokeStyle='rgba(255,255,255,.1)'; c.lineWidth=1.4; c.stroke();
    bigNum(c,cd[0],x+40,y+120,92,1); c.save(); c.font='400 28px Inter'; wrapText(c,cd[1],x+42,y+176,w-84,38,C.dim); c.restore();
    const gx=x+40,gy=y+260,gw=w-80,gh=h-300;
    if(i===0){ for(let r=0;r<5;r++){ const yy=gy+6+r*38; const a=hash(r*7+i)*255|0; for(let q=0;q<9;q++){ const v=((hash(r*13+q*5)*255)|0).toString(16).padStart(2,'0').toUpperCase(); txt(c,v,gx+q*50,yy+24,{size:24,mono:true,col:C.dim}); } }
      const ck=eo5(seg(bt,2.0,2.8)); c.save(); c.globalAlpha=ck; c.fillStyle=PAL.ok; c.beginPath(); c.arc(gx+gw-30,gy+gh-34,30,0,7); c.fill(); c.strokeStyle='#04140b'; c.lineWidth=7; c.lineCap='round'; c.beginPath(); c.moveTo(gx+gw-44,gy+gh-33); c.lineTo(gx+gw-33,gy+gh-22); c.lineTo(gx+gw-15,gy+gh-46); c.stroke(); txt(c,'identical',gx,gy+gh-18,{size:26,w:600,col:PAL.ok,a:1}); c.restore(); }
    else if(i===1){ plotFrame(c,gx,gy,gw,gh*.75); const sg=[{u:.5,w:.3,k:1}]; c.save(); c.beginPath(); c.rect(gx,gy,gw,gh*.75); c.clip(); c.strokeStyle=PAL.accent; c.lineWidth=2.4; c.beginPath(); for(let j=0;j<=240;j++){ const u=j/240, d=specDb(u,bt+3,sg); const px=gx+u*gw, py=gy+gh*.75-clamp((d+100)/68)*gh*.75; j?c.lineTo(px,py):c.moveTo(px,py);} c.stroke(); c.restore(); txt(c,'522.000 MHz',gx,gy+gh-8,{size:26,mono:true,w:500,col:PAL.accent}); }
    else { for(let r=0;r<5;r++)for(let q=0;q<14;q++){ const on=clamp((bt-3.0)*14-(q+r))/1; const sh=.6+.4*Math.sin(bt*4+q*.6+r); c.fillStyle=on>0?acc(.4+.55*sh):'rgba(255,255,255,.07)'; c.fillRect(gx+q*37,gy+r*38,31,31); } txt(c,'GPU · SIMD',gx,gy+gh-8,{size:26,w:600,col:PAL.accent}); }
    c.restore(); });
};
function wrapText(c,s,x,y,maxW,lh,col){ const words=s.split(' '); let line='',yy=y; c.fillStyle=col; for(const w of words){ const t=line?line+' '+w:w; if(c.measureText(t).width>maxW&&line){ c.fillText(line,x,yy); line=w; yy+=lh; } else line=t; } c.fillText(line,x,yy); }
// ---------------------------------------------------------------- outro
T.outro=(c,bt)=>{
  bgd(c,1);
  const lk=eoBack(seg(bt,.2,1.0))*(1+.02*bp(bt)*seg(bt,1,1.5)); c.save(); c.translate(W/2,262); c.scale(lk,lk); c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=60; c.shadowOffsetY=24; c.globalAlpha=seg(bt,.2,.6); c.drawImage(logo,-160,-160,320,320); c.restore();
  const p=.5+.5*Math.sin(bt*3.2); const gg=c.createRadialGradient(W/2+90,285,0,W/2+90,285,220); gg.addColorStop(0,`rgba(224,97,87,${.16+.14*p})`); gg.addColorStop(1,'rgba(224,97,87,0)'); c.fillStyle=gg; c.globalAlpha=seg(bt,.8,1.4); c.fillRect(W/2-300,120,800,400); c.globalAlpha=1;
  reveal(c,'OnAir',W/2,595,bt,1.0,{size:150,w:600,ls:-6,al:'center'});
  reveal(c,'Open-source digital TV receiver for your SDR.',W/2,665,bt,1.5,{size:40,w:400,col:C.dim,ls:-.8,al:'center'});
  const row1=['DVB-T2','T2-Lite','DVB-T','ATSC'], w1=row1.map(s=>s.length*14+64), t1=w1.reduce((a,b)=>a+b+16,-16); let cx=W/2-t1/2;
  row1.forEach((s,i)=>{ chip(c,s,cx+w1[i]/2,740,eo5(seg(bt,2.0+i*.5,2.6+i*.5)),{size:22,pad:40}); cx+=w1[i]+16; });
  const k=eo5(seg(bt,4.0,4.8)); c.save(); c.globalAlpha=k; c.translate(0,(1-k)*20);
  const plat=[['macOS',1],['Linux',1]]; const pw=[200,200]; let px=W/2-(pw[0]+pw[1]+16)/2;
  plat.forEach((pl,i)=>{ rr(c,px,810,pw[i],62,31); c.fillStyle=acc(.14); c.fill(); c.strokeStyle=PAL.accent; c.lineWidth=2; c.stroke(); txt(c,pl[0],px+pw[i]/2,850,{size:28,w:600,al:'center'}); px+=pw[i]+16; });
  txt(c,'Runs natively on macOS and Linux*',W/2,920,{size:26,w:500,al:'center',col:C.ink}); c.restore();
  reveal(c,'github.com/adswill/OnAir',W/2,985,bt,5.0,{size:30,w:500,col:PAL.accent,mono:true,al:'center'});
  reveal(c,'* Windows coming soon.',W/2,1038,bt,5.5,{size:22,w:400,col:C.dim,al:'center'});
};

// ---------------------------------------------------------------- timeline: scene, bar-line start, bar-line end, transition into it, overlap
const B=[['hook',0,6,null,0],['logo',6,12,'zoom',.7],['radios',12,20,'iris',.8],['auto',20,28,'sweep',.8],['live',28,36,'push',.7],['lite',36,44,'zoom',.7],['exact',44,50,'slices',.7],['see',50,58,'iris',.8],['fix',58,68,'sweep',.8],['out',68,74,'push',.7],['outro',74,82,'zoom',.9]];
const SCENES=B.map((s,i)=>{ const next=B[i+1]; const t0=s[1]-(s[4]||0)/2; const t1=s[2]+(next?next[4]/2:0); const o=[s[0],t1-t0,s[3],s[4]||0]; o.t0=t0; o.t1=t1; o.start=s[1]; return o; });
const TOTAL=82; window.TOTAL=TOTAL; window.NF=Math.round(TOTAL*FPS);
window.SCENES=SCENES.map(s=>({name:s[0],t0:s.t0,t1:s.t1,tr:s[2],trd:s[3]}));
const bufA=mk(), bufB=mk(), ca=bufA.getContext('2d'), cb=bufB.getContext('2d');
function sceneAt(c,s,t){ c.clearRect(0,0,W,H); CURBT=t-s.start; T[s[0]](c,t-s.start); }
function composite(t){
  const act=SCENES.filter(s=>t>=s.t0&&t<s.t1);
  if(act.length===1){ sceneAt(ctx,act[0],t); return; }
  const A=act[0],Bn=act[1], p=clamp((t-Bn.t0)/Bn[3]); sceneAt(ca,A,t); sceneAt(cb,Bn,t);
  const e=eio(p), type=Bn[2]; ctx.fillStyle='#000'; ctx.fillRect(0,0,W,H);
  if(type==='iris'){ ctx.drawImage(bufA,0,0); ctx.save(); const r=e*Math.hypot(W,H)*.75, ox=W/2, oy=H*.42; ctx.beginPath(); ctx.arc(ox,oy,r,0,7); ctx.clip(); ctx.drawImage(bufB,0,0); ctx.restore();
    ctx.save(); ctx.strokeStyle=PAL.accent; ctx.globalAlpha=(1-e)*.9; ctx.lineWidth=3; ctx.shadowColor=PAL.accent; ctx.shadowBlur=30; ctx.beginPath(); ctx.arc(ox,oy,r,0,7); ctx.stroke(); ctx.restore(); }
  else if(type==='sweep'){ ctx.drawImage(bufA,0,0); const x=lerp(-300,W+300,e), sk=240; ctx.save(); ctx.beginPath(); ctx.moveTo(0,0); ctx.lineTo(x+sk,0); ctx.lineTo(x-sk,H); ctx.lineTo(0,H); ctx.closePath(); ctx.clip(); ctx.drawImage(bufB,0,0); ctx.restore();
    ctx.save(); const g=ctx.createLinearGradient(x-90,0,x+90,0); g.addColorStop(0,'rgba(92,168,219,0)'); g.addColorStop(.5,'rgba(190,225,245,.55)'); g.addColorStop(1,'rgba(92,168,219,0)'); ctx.fillStyle=g; ctx.globalAlpha=Math.sin(p*Math.PI); ctx.beginPath(); ctx.moveTo(x+sk-90,0); ctx.lineTo(x+sk+90,0); ctx.lineTo(x-sk+90,H); ctx.lineTo(x-sk-90,H); ctx.closePath(); ctx.fill(); ctx.restore(); }
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
window.renderFrame=async(i)=>{ const t=i/FPS; composite(t); post(t); return t; };
