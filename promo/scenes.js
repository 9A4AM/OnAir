// OnAir promo v5: demonstrations drawn on a canvas in the app's own palette (single accent colour, navy waterfall ramp,
// accent-coloured constellations on near-black plots), with depth: glass cards, a 3D waterfall, particle constellations,
// bloom, dust, camera drift and motion-blurred transitions. Scene functions get bt = seconds since the scene's bar line.
// Needs common_head.js. Every on-screen cue sits on the 120 BPM beat grid, matching music.py.
Object.assign(C,{blue:'#5ca8db',green:'#66c285',amber:'#e6b347',red:'#e06157',ink:'#e0e5eb',dim:'#7f8c99',dim2:'#4d565f'});
const PAL={accent:'#5ca8db',ok:'#66c285',warn:'#e6b347',bad:'#e06157',grey:'#9eabba',plot:'#030405',border:'#383f47',panel:'#141619'};
const acc=a=>`rgba(92,168,219,${a})`;
let CURBT=0, CURT=0; const bp=bt=>Math.exp(-7*(((bt%.5)+.5)%.5));      // 1 on every beat, decays before the next
const pop=(bt,t0)=>bt<t0?0:Math.exp(-9*(bt-t0));                      // spring kick right after a cue

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
  c.strokeStyle=PAL.accent; c.lineWidth=o.lw||2.2; c.lineJoin='round'; c.shadowColor=PAL.accent; c.shadowBlur=o.glow??10; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke();
  c.restore();
  if(o.axes){ for(let i=0;i<=8;i++) txt(c,(518+i)+'',x+(i+.5)*w/9.0,y+h+26,{size:o.fs||15,mono:true,col:C.dim,al:'center'}); }
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
    if(d>.8){ c.shadowColor=PAL.accent; c.shadowBlur=18*d; } c.stroke(); c.restore(); }
}
// constellation whose symbols fly in from noise and snap onto the QAM grid (k: 0 scattered ... 1 locked)
function constelFX(c,x,y,s,M,k,t,o={}){
  plotFrame(c,x,y,s,s); const side=Math.sqrt(M), cell=s/(side+.6), n=o.n||1800, sig=o.sigma??.045, cx=x+s/2, cy=y+s/2;
  c.save(); c.beginPath(); c.rect(x+2,y+2,s-4,s-4); c.clip();
  const gk=eo3(clamp((k-.35)/.65)); c.strokeStyle=`rgba(255,255,255,${.13*gk})`; c.lineWidth=1; c.beginPath();
  if(M>4) for(let a=0;a<side;a++){const q=(a-(side-1)/2)*cell; c.moveTo(cx+q,y+6);c.lineTo(cx+q,y+s-6);c.moveTo(x+6,cy+q);c.lineTo(x+s-6,cy+q);}
  c.moveTo(cx,y);c.lineTo(cx,y+s);c.moveTo(x,cy);c.lineTo(x+s,cy);c.stroke();
  const ds=o.dot||Math.max(2.4,s/230);
  const at=(i,kk)=>{ const idx=Math.floor(hash(i*3.1+7)*M), a=idx%side, b=Math.floor(idx/side)%side;
    const tx=(a-(side-1)/2)*cell+gauss(i)*sig*cell, ty=(b-(side-1)/2)*cell+gauss(i+9999)*sig*cell;
    const ang=hash(i*5.3)*6.2832, rad=(.12+hash(i*9.1)*.85)*s*.5, sx=Math.cos(ang)*rad, sy=Math.sin(ang)*rad;
    const st=hash(i*1.7)*.4, q=eo5(clamp((kk*1.4-st)/1.0)); const sw=(1-q)*(hash(i*2.9)>.5?1.6:-1.6);
    let px=lerp(sx,tx,q), py=lerp(sy,ty,q); const ca=Math.cos(sw), sa=Math.sin(sw); return [cx+px*ca-py*sa, cy+px*sa+py*ca, q]; };
  for(let i=0;i<n;i++){ const [px,py,q]=at(i,k); if(q<.97){ const [qx,qy]=at(i,Math.max(0,k-.035)); c.strokeStyle=acc(.22*(1-q)); c.lineWidth=1.2; c.beginPath(); c.moveTo(qx,qy); c.lineTo(px,py); c.stroke(); }
    c.fillStyle=q>.97?acc(.82+.1*bp(t)):acc(.35+.5*q); const z=ds*(q>.97?1:1.25); c.fillRect(px-z/2,py-z/2,z,z); }
  c.restore();
}
function lamp(c,x,y,on,label,size=26){ c.fillStyle=on>0?PAL.ok:'#434a52'; c.beginPath(); c.arc(x,y,size*.3,0,7); c.fill(); if(on>0){c.save();c.globalAlpha=.5*on;c.shadowColor=PAL.ok;c.shadowBlur=20;c.beginPath();c.arc(x,y,size*.3,0,7);c.fill();c.restore();} if(label) txt(c,label,x+size*.65,y+size*.35,{size,w:500,col:on>0?C.ink:C.dim}); }

// ---------------------------------------------------------------- depth: backdrop, glass, dust, type
function dust(c,a=1){ c.save(); for(let i=0;i<70;i++){ const sp=6+hash(i*5.1)*20, sz=.8+hash(i*3.3)*2.2, x=(hash(i*1.9)*W+Math.sin(CURT*.3+i)*20+W)%W, y=((hash(i*7.7)*H-CURT*sp)%H+H)%H, tw=.5+.5*Math.sin(CURT*1.5+i*2.1);
  c.fillStyle=i%5===0?`rgba(92,168,219,${.28*tw*a})`:`rgba(255,255,255,${.12*tw*a*(sz/3)})`; c.beginPath(); c.arc(x,y,sz,0,7); c.fill(); } c.restore(); }
function bgd(c,glow=.7){ c.fillStyle=C.bg; c.fillRect(0,0,W,H);
  const g=c.createRadialGradient(W*.7,H*.55,0,W*.7,H*.55,W*.75); g.addColorStop(0,`rgba(40,110,170,${(.14+.05*bp(CURBT))*glow})`); g.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g; c.fillRect(0,0,W,H);
  const g2=c.createRadialGradient(W*.1,H*.1,0,W*.1,H*.1,W*.5); g2.addColorStop(0,`rgba(60,80,140,${.08*glow})`); g2.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g2; c.fillRect(0,0,W,H);
  c.save(); c.lineWidth=2; for(let k=0;k<3;k++){ const u=((CURBT/2+k/3)%1+1)%1, r=60+u*1100; c.strokeStyle=`rgba(92,168,219,${.07*(1-u)*glow})`; c.beginPath(); c.arc(W*.86,H*.5,r,0,7); c.stroke(); } c.restore();
  dust(c,glow); }
function glass(c,x,y,w,h,r=22,o={}){
  c.save(); c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=o.blur??46; c.shadowOffsetY=o.dy??20; rr(c,x,y,w,h,r);
  const g=c.createLinearGradient(0,y,0,y+h); g.addColorStop(0,o.top||'#161b21'); g.addColorStop(1,o.bot||'#0a0d10'); c.fillStyle=g; c.fill(); c.shadowColor='transparent';
  const s=c.createLinearGradient(0,y,0,y+h*.55); s.addColorStop(0,'rgba(255,255,255,.075)'); s.addColorStop(1,'rgba(255,255,255,0)'); rr(c,x,y,w,h,r); c.fillStyle=s; c.fill();
  const b=c.createLinearGradient(x,y,x+w*.7,y+h); b.addColorStop(0,o.active?'rgba(120,190,235,.95)':'rgba(255,255,255,.30)'); b.addColorStop(1,o.active?'rgba(92,168,219,.30)':'rgba(255,255,255,.06)');
  rr(c,x+.75,y+.75,w-1.5,h-1.5,r); c.strokeStyle=b; c.lineWidth=1.6; c.stroke();
  if(o.active){ c.shadowColor=PAL.accent; c.shadowBlur=34; c.globalAlpha=.4; c.stroke(); } c.restore(); }
// per-letter kinetic type (rise + fade), returns the width; al: 'left' | 'center'
function kinetic(c,s,x,y,bt,t0,o={}){
  const size=o.size||64, w=o.w||600, ls=(o.ls??-size*.03), st=o.stagger??.03, col=o.col||C.ink;
  c.save(); c.font=`${w} ${size}px Inter`; c.letterSpacing='0px'; const adv=[...s].map(ch=>c.measureText(ch).width+ls), tot=adv.reduce((a,b)=>a+b,0);
  let px=o.al==='center'?x-tot/2:x; [...s].forEach((ch,i)=>{ const k=eo5(seg(bt,t0+i*st,t0+i*st+.55)); if(k>0){ c.save(); c.globalAlpha*=k; c.translate(px,y+(1-k)*size*.45); c.rotate((1-k)*.06); c.font=`${w} ${size}px Inter`; c.fillStyle=col; c.fillText(ch,0,0); c.restore(); } px+=adv[i]; });
  c.restore(); return tot; }
function sheen(c,s,x,y,bt,t0,o={}){ // text with a light band travelling across it
  const size=o.size||120; c.save(); c.font=`${o.w||600} ${size}px Inter`; c.letterSpacing=(o.ls??-size*.04)+'px'; const tw=c.measureText(s).width, x0=o.al==='center'?x-tw/2:x;
  const k=eo5(seg(bt,t0,t0+.9)); c.globalAlpha*=k; const p=clamp((bt-t0-.5)/1.2), g=c.createLinearGradient(x0,0,x0+tw,0), base='#e6ebf0'; g.addColorStop(0,base); g.addColorStop(clamp(p-.14),base); g.addColorStop(clamp(p),'#ffffff'); g.addColorStop(clamp(p+.14),base); g.addColorStop(1,base);
  c.fillStyle=g; c.textAlign='left'; c.translate(0,(1-k)*30); c.fillText(s,x0,y); c.restore(); }
function head(c,l1,l2,t,t0,o={}){ const x=o.x??110,y=o.y??140,s=o.size??64; kinetic(c,l1,x,y,t,t0,{size:s,w:500,ls:-s*.035,stagger:.02,al:o.al||'left'}); if(l2) kinetic(c,l2,x,y+s*1.08,t,t0+.25,{size:s,w:500,ls:-s*.035,stagger:.02,col:o.col||PAL.accent,al:o.al||'left'}); }
function chip(c,s,cx,cy,a=1,o={}){ c.save(); c.globalAlpha*=a; const sc=1+.05*(o.pop||0); c.translate(cx,cy); c.scale(sc,sc); c.translate(-cx,-cy); c.font=`${o.w||500} ${o.size||24}px Inter`; const tw=c.measureText(s).width+(o.pad||44), th=(o.size||24)+26;
  rr(c,cx-tw/2,cy-th/2,tw,th,th/2); const g=c.createLinearGradient(0,cy-th/2,0,cy+th/2); g.addColorStop(0,o.fill||'rgba(255,255,255,.07)'); g.addColorStop(1,'rgba(255,255,255,.02)'); c.fillStyle=g; c.fill(); c.strokeStyle=o.stroke||'rgba(255,255,255,.2)'; c.lineWidth=1.4; c.stroke();
  txt(c,s,cx,cy+(o.size||24)*.35,{size:o.size||24,w:o.w||500,al:'center',col:o.col||C.ink}); c.restore(); return tw; }
const flash=(c,a)=>{ if(a>0){c.save();c.fillStyle=`rgba(255,255,255,${a})`;c.fillRect(0,0,W,H);c.restore();} };
function shock(c,x,y,t0,bt,col='92,168,219'){ const u=(bt-t0)/.9; if(u<0||u>1) return; c.save(); c.strokeStyle=`rgba(${col},${.8*(1-u)})`; c.lineWidth=6*(1-u)+1; c.shadowColor=`rgba(${col},1)`; c.shadowBlur=30; c.beginPath(); c.arc(x,y,40+eo3(u)*900,0,7); c.stroke(); c.restore(); }
function burst(c,x,y,bt,t0,n=80,col='92,168,219'){ if(bt<t0||bt>t0+1.8) return; const u=bt-t0; c.save(); for(let i=0;i<n;i++){ const a=hash(i*3.7)*6.2832, sp=180+hash(i*5.9)*900, d=sp*(1-Math.exp(-u*2.2))/2.2, px=x+Math.cos(a)*d, py=y+Math.sin(a)*d+u*u*60, life=1-u/1.8;
  c.strokeStyle=`rgba(${col},${.7*life})`; c.lineWidth=2.2*life+.4; c.beginPath(); c.moveTo(px-Math.cos(a)*sp*.06,py-Math.sin(a)*sp*.06); c.lineTo(px,py); c.stroke(); } c.restore(); }
function scan(c,a=.12){ c.save(); c.fillStyle=`rgba(0,0,0,${a})`; for(let y=0;y<H;y+=4) c.fillRect(0,y+2,W,2); c.restore(); }
function bez(a,b,c2,d,t){const u=1-t;return u*u*u*a+3*u*u*t*b+3*u*t*t*c2+t*t*t*d}
function wrapText(c,s,x,y,maxW,lh,col){ const words=s.split(' '); let line='',yy=y; c.fillStyle=col; for(const w of words){ const t=line?line+' '+w:w; if(c.measureText(t).width>maxW&&line){ c.fillText(line,x,yy); line=w; yy+=lh; } else line=t; } c.fillText(line,x,yy); }
function persp(c,src,cx,cy,sc,angDeg,alpha=1){ const sw=src.width, sh=src.height, ang=angDeg*Math.PI/180, strip=3, f=2600; c.save(); c.globalAlpha*=alpha;
  for(let x=0;x<sw;x+=strip){ const lx=(x-sw/2)*sc, z=lx*Math.sin(ang), px=lx*Math.cos(ang), s=f/(f+z), h=sh*sc*s; c.drawImage(src,x,0,strip,sh,cx+px*s,cy-h/2,strip*sc*Math.cos(ang)*s+.6,h); } c.restore(); }

// ---- a TV picture (colour bars, a bouncing ball, a station lower third): broadcast content, not app UI
function tvPicture(c,x,y,w,h,t,o={}){
  c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip();
  const bars=['#c4c4c4','#c4c400','#00c4c4','#00c400','#c400c4','#c40000','#0000c4'], bw=w/7;
  bars.forEach((b,i)=>{c.fillStyle=b;c.fillRect(x+i*bw,y,bw+1,h*.66)});
  ['#0000c4','#0a0a0a','#c400c4','#0a0a0a','#00c4c4','#0a0a0a','#c4c4c4'].forEach((b,i)=>{c.fillStyle=b;c.fillRect(x+i*bw,y+h*.66,bw+1,h*.08)});
  c.fillStyle='#0b0e14'; c.fillRect(x,y+h*.74,w,h*.26); c.fillStyle='#f2f2f2'; c.fillRect(x+bw*1.1,y+h*.74,bw*1.5,h*.26); c.fillStyle='#2a0f55'; c.fillRect(x+bw*2.7,y+h*.74,bw*1.5,h*.26);
  const bx=x+w*(.5+.42*Math.sin(t*1.7)), by=y+h*(.33+.27*Math.abs(Math.sin(t*2.3))); c.fillStyle='#fff'; c.beginPath(); c.arc(bx,by,h*.055,0,7); c.fill(); c.fillStyle='rgba(0,0,0,.25)'; c.beginPath(); c.arc(bx+h*.012,by+h*.012,h*.055,0,7); c.fill();
  const lt=eo5(seg(t,o.lt??.6,(o.lt??.6)+.9)); if(!o.noLower){ c.save(); c.translate(-(1-lt)*w*.6,0); rr(c,x+w*.04,y+h*.8,w*.46,h*.14,h*.012); c.fillStyle='rgba(8,12,18,.9)'; c.fill(); c.fillStyle=PAL.bad; c.fillRect(x+w*.04,y+h*.8,h*.012,h*.14);
    txt(c,'Channel One HD',x+w*.04+h*.04,y+h*.8+h*.06,{size:h*.047,w:600}); txt(c,'Evening News  18:00',x+w*.04+h*.04,y+h*.8+h*.11,{size:h*.032,col:'#b9c4cf',w:400}); c.restore(); }
  rr(c,x+w*.84,y+h*.05,w*.12,h*.075,h*.012); c.fillStyle=PAL.bad; c.fill(); txt(c,'ON AIR',x+w*.9,y+h*.05+h*.052,{size:h*.04,w:700,al:'center',col:'#fff'});
  c.fillStyle='rgba(0,0,0,.14)'; for(let yy=0;yy<h;yy+=4) c.fillRect(x,y+yy+2,w,2);
  c.restore();
}
// small vector SDR boxes for the radios scene
function devIcon(c,kind,x,y,t){ const w=[96,84,88,70,92,100,108,82][kind], h=[54,44,56,64,64,66,50,26][kind];
  c.save(); c.translate(x,y+(60-h)/2); const g=c.createLinearGradient(0,0,0,h); g.addColorStop(0,'#323a43'); g.addColorStop(1,'#171b20'); rr(c,0,0,w,h,kind===7?13:8); c.fillStyle=g; c.fill(); c.strokeStyle='rgba(255,255,255,.22)'; c.lineWidth=1.4; c.stroke();
  c.fillStyle='#0b0e11';
  if(kind===0){ rr(c,10,10,w*.45,h-20,4); c.fill(); c.fillStyle='#cfd6dd'; c.fillRect(w-26,8,9,14); c.fillRect(w-26,h-22,9,14); }
  if(kind===1){ rr(c,8,h/2-5,w-34,10,4); c.fill(); c.fillStyle='#cfd6dd'; c.fillRect(w-18,h/2-6,10,12); }
  if(kind===2){ for(let i=0;i<3;i++){ c.beginPath(); c.arc(16+i*16,h/2,5,0,7); c.fill(); } c.fillStyle='#cfd6dd'; c.fillRect(w-16,h/2-6,9,12); }
  if(kind===3){ rr(c,8,8,w-16,h-16,6); c.fill(); c.fillStyle='#cfd6dd'; c.fillRect(-6,h/2-9,8,18); }
  if(kind===4){ for(let i=0;i<7;i++) c.fillRect(12+i*10,10,5,h-20); c.fillStyle='#cfd6dd'; c.fillRect(w-12,10,7,12); }
  if(kind===5){ rr(c,10,10,22,22,3); c.fill(); rr(c,40,10,22,22,3); c.fill(); rr(c,10,38,52,16,3); c.fill(); c.fillStyle='#cfd6dd'; c.fillRect(w-14,10,8,12); c.fillRect(w-14,h-24,8,12); }
  if(kind===6){ for(let i=0;i<4;i++){ c.beginPath(); c.arc(14+i*20,h/2,6,0,7); c.fill(); } c.fillStyle='#cfd6dd'; c.fillRect(w-12,h/2-8,7,16); }
  if(kind===7){ c.fillStyle='#cfd6dd'; c.fillRect(w-2,h/2-4,8,8); c.fillRect(w+6,h/2-1.5,16,3); c.fillStyle='#0b0e11'; rr(c,8,6,w-30,h-12,6); c.fill(); }
  c.fillStyle=PAL.ok; c.globalAlpha=.55+.45*bp(t); c.shadowColor=PAL.ok; c.shadowBlur=12; c.beginPath(); c.arc(w-10-(kind===7?10:0),h-9,3.2,0,7); c.fill(); c.restore(); }

const T={};
// ---------------------------------------------------------------- hook: Tune. Lock. Watch.
T.hook=(c,bt)=>{
  c.fillStyle='#000'; c.fillRect(0,0,W,H);
  const punch=t0=>1.12-.12*eo5(seg(bt,t0,t0+.55));
  if(bt<2.0){ const s=punch(0); c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
    const k=eo3(seg(bt,.15,1.5)); const g0=c.createRadialGradient(W/2,H*.55,0,W/2,H*.55,W*.6); g0.addColorStop(0,'rgba(40,110,170,.18)'); g0.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g0; c.fillRect(0,0,W,H);
    ridge(c,60,70,1800,850,bt+8,[{u:.5,w:.3,k}],{rows:34,n:170,dt:.2});
    for(let i=0;i<=8;i++) txt(c,(518+i)+'',W*(.12+i*.095),H-96,{size:20,mono:true,col:C.dim,al:'center',a:.8});
    c.restore();
    const g=c.createLinearGradient(0,H*.55,0,H); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.72)'); c.fillStyle=g; c.fillRect(0,H*.55,W,H*.45);
    const f=lerp(517.6,522.0,eo3(seg(bt,.1,1.5))); txt(c,f.toFixed(3)+' MHz',1840,96,{size:56,w:500,mono:true,al:'right'});
    kinetic(c,'Tune.',100,1010,bt,.02,{size:210,w:700,ls:-9,stagger:.06}); }
  else if(bt<4.0){ const tt=bt-2, s=punch(2); c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
    c.fillStyle='#050607'; c.fillRect(0,0,W,H); const g0=c.createRadialGradient(1390,540,0,1390,540,700); g0.addColorStop(0,'rgba(40,110,170,.2)'); g0.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=g0; c.fillRect(0,0,W,H);
    constelFX(c,1000,150,780,64,eo3(seg(tt,.2,1.65)),bt,{n:2400,dot:4.2}); c.restore();
    kinetic(c,'Lock.',100,1010,bt,2.02,{size:210,w:700,ls:-9,stagger:.06});
    const mer=lerp(8.2,31.4,eo3(seg(tt,.3,1.6))); txt(c,'MER',110,170,{size:26,w:500,col:C.dim}); txt(c,mer.toFixed(1)+' dB',110,236,{size:64,w:500,mono:true,col:PAL.accent});
    ['IQ','P1','L1-pre','LDPC'].forEach((n,i)=>lamp(c,128,330+i*58,eo3(seg(bt,2.5+i*.25,2.75+i*.25)),n,30)); }
  else { const tt=bt-4, s=punch(4)*(1.0); c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
    tvPicture(c,0,0,W,H,bt,{lt:.5}); c.restore(); scan(c,.16);
    const vg=c.createRadialGradient(W/2,H/2,H*.35,W/2,H/2,H*.95); vg.addColorStop(0,'rgba(0,0,0,0)'); vg.addColorStop(1,'rgba(0,0,0,.6)'); c.fillStyle=vg; c.fillRect(0,0,W,H);
    const g=c.createLinearGradient(0,H*.5,0,H); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.82)'); c.fillStyle=g; c.fillRect(0,H*.5,W,H*.5);
    c.save(); c.globalAlpha=.35+.65*bp(bt); c.shadowColor=PAL.bad; c.shadowBlur=44; c.strokeStyle='rgba(255,140,130,.9)'; c.lineWidth=3; rr(c,W*.84,H*.05,W*.12,H*.075,12); c.stroke(); c.restore();
    kinetic(c,'Watch.',100,1010,bt,4.02,{size:210,w:700,ls:-9,stagger:.06}); }
  flash(c,.5*(1-seg(bt,0,.12))); flash(c,(bt>=2?.45:0)*(1-seg(bt,2,2.14))); flash(c,(bt>=4?.45:0)*(1-seg(bt,4,4.14)));
  shock(c,W/2,H/2,2.0,bt); shock(c,W/2,H/2,4.0,bt);
};
T.logo=(c,bt)=>{
  bgd(c,seg(bt,0,1));
  c.save(); c.globalAlpha=.7*eo3(seg(bt,.5,2)); ridge(c,0,690,W,420,bt+3,[{u:.5,w:.3,k:1}],{rows:18,n:140,dt:.25,a:.5}); c.restore();
  const lk=eoBack(seg(bt,.1,.9))*(1+.03*bp(bt)*seg(bt,1,1.5)); c.save(); c.translate(W/2,330); c.scale(lk,lk);
  const gl=c.createRadialGradient(0,0,0,0,0,420); gl.addColorStop(0,'rgba(224,97,87,.26)'); gl.addColorStop(.5,'rgba(224,97,87,.08)'); gl.addColorStop(1,'rgba(224,97,87,0)'); c.fillStyle=gl; c.fillRect(-460,-460,920,920);
  c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=50; c.shadowOffsetY=20; c.globalAlpha=seg(bt,.1,.4); c.drawImage(logo,-170,-170,340,340); c.restore();
  burst(c,W/2,330,bt,.1,90); shock(c,W/2,330,.1,bt);
  const fl=seg(bt,.1,.5)*(1-seg(bt,.5,1.2)); if(fl>0){ c.save(); const g=c.createLinearGradient(W/2-700,0,W/2+700,0); g.addColorStop(0,'rgba(255,255,255,0)'); g.addColorStop(.5,`rgba(255,255,255,${.9*fl})`); g.addColorStop(1,'rgba(255,255,255,0)'); c.fillStyle=g; c.fillRect(W/2-700,328,1400,4); c.restore(); }
  sheen(c,'OnAir',W/2,700,bt,.9,{size:132,al:'center'});
  kinetic(c,'Digital TV, decoded in software.',W/2,780,bt,1.5,{size:46,w:400,ls:-1,stagger:.018,al:'center'});
  ['DVB-T2','DVB-T','ATSC'].forEach((s,i)=>chip(c,s,W/2+(i-1)*210,850,eoBack(seg(bt,2.4+i*.5,3.0+i*.5)),{size:26,pop:pop(bt,2.5+i*.5)}));
};
// ---------------------------------------------------------------- any radio
const RADIOS=[['HackRF One / Pro','native driver'],['Airspy','via SoapySDR'],['SDRplay RSP','via SoapySDR'],['PlutoSDR','via SoapySDR'],['BladeRF','via SoapySDR'],['LimeSDR','via SoapySDR'],['USRP','via SoapySDR'],['RTL-SDR','narrow signals only']];
T.radios=(c,bt)=>{
  bgd(c,.9);
  head(c,'Bring your own radio.',null,bt,.2,{size:66,y:150});
  reveal(c,'HackRF natively. Everything else through SoapySDR.',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  const lx=110, ly=290, cw=480, ch=104, gx=26, gy=20, logoC=[1540,560];
  // hub: orbit rings + glow
  const lk=eoBack(seg(bt,.6,1.6)); c.save(); c.translate(logoC[0],logoC[1]); c.scale(lk,lk); const gl=c.createRadialGradient(0,0,0,0,0,330); gl.addColorStop(0,'rgba(92,168,219,.22)'); gl.addColorStop(1,'rgba(92,168,219,0)'); c.fillStyle=gl; c.fillRect(-340,-340,680,680);
  for(let k=0;k<2;k++){ const r=170+k*50; c.strokeStyle=`rgba(92,168,219,${.28-k*.1})`; c.lineWidth=1.6; c.setLineDash([3,12]); c.beginPath(); c.arc(0,0,r,0,7); c.stroke(); c.setLineDash([]); const a=bt*(k?-.6:.8)+k*2; c.fillStyle=PAL.accent; c.shadowColor=PAL.accent; c.shadowBlur=16; c.beginPath(); c.arc(Math.cos(a)*r,Math.sin(a)*r,5.5,0,7); c.fill(); c.shadowBlur=0; }
  c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=40; c.shadowOffsetY=16; c.globalAlpha=lk; c.drawImage(logo,-125,-125,250,250); c.restore();
  kinetic(c,'OnAir',logoC[0],logoC[1]+235,bt,1.2,{size:46,w:600,al:'center',stagger:.04});
  RADIOS.forEach((r,i)=>{ const col=i%2, row=Math.floor(i/2); const x=lx+col*(cw+gx), y=ly+row*(ch+gy); const a=eo5(seg(bt,1.0+i*.5,1.6+i*.5)); if(a<=0) return;
    const x1=x+cw, y1=y+ch/2; c.save(); c.globalAlpha=a*.5; c.strokeStyle=PAL.accent; c.lineWidth=1.8; c.beginPath(); c.moveTo(x1,y1); c.bezierCurveTo(x1+250,y1,logoC[0]-340,logoC[1],logoC[0]-175,logoC[1]); c.stroke(); c.restore();
    for(let p=0;p<4;p++){ const u=((bt*.38+p/4+i*.17)%1); const bx=bez(x1,x1+250,logoC[0]-340,logoC[0]-175,u), by=bez(y1,y1,logoC[1],logoC[1],u); c.save(); c.globalAlpha=a*Math.sin(u*Math.PI); c.fillStyle='#bfe3fa'; c.shadowColor=PAL.accent; c.shadowBlur=18; c.beginPath(); c.arc(bx,by,4.5,0,7); c.fill(); c.restore(); } });
  RADIOS.forEach((r,i)=>{ const col=i%2, row=Math.floor(i/2); const x=lx+col*(cw+gx), y=ly+row*(ch+gy); const a=eoBack(seg(bt,1.0+i*.5,1.7+i*.5)); if(a<=0) return;
    c.save(); c.globalAlpha=clamp(a); const pu=1+.03*pop(bt,1.0+i*.5); c.translate(x+cw/2,y+ch/2); c.scale(pu,pu); c.translate(-(x+cw/2),-(y+ch/2)); c.translate((1-clamp(a))*-50,0);
    glass(c,x,y,cw,ch,18,{active:i===0,blur:30,dy:12}); devIcon(c,i,x+22,y+22,bt+i*.13);
    txt(c,r[0],x+152,y+48,{size:30,w:600,ls:-.4}); txt(c,r[1],x+152,y+80,{size:20,col:i===0?PAL.accent:C.dim,w:400}); c.restore(); });
  reveal(c,'A radio needs about 1 Msps per MHz of channel width.',110,1010,bt,5.2,{size:24,w:400,col:C.dim});
};
// ---------------------------------------------------------------- auto detection + scanner
T.auto=(c,bt)=>{
  bgd(c,.9);
  head(c,'It finds the signal.','You don’t configure a thing.',bt,.2,{size:60,y:140});
  const cards=[['DVB-T2','T2-Base and T2-Lite','1.7 – 10 MHz · up to 256-QAM',.34,'ofdm'],['DVB-T','2K and 8K','QPSK – 64-QAM · all guard intervals',.26,'ofdm'],['ATSC 1.0','8-VSB','6 MHz · decision-feedback equaliser',.30,'vsb']];
  const sel=bt<2.5?-1:Math.min(2,Math.floor((bt-2.5)/.5));
  cards.forEach((cd,i)=>{ const x=110+i*590, y=300, w=550, h=330, k=eoBack(seg(bt,.7+i*.15,1.7+i*.15)); if(k<=0) return; const on=sel===i;
    c.save(); c.globalAlpha=clamp(k); c.translate(0,(1-clamp(k))*40); const pu=on?1+.012*bp(bt):1; c.translate(x+w/2,y+h/2); c.scale(pu,pu); c.translate(-(x+w/2),-(y+h/2));
    glass(c,x,y,w,h,22,{active:on}); kinetic(c,cd[0],x+30,y+66,bt,.9+i*.15,{size:48,w:600,ls:-1.5,stagger:.03}); txt(c,cd[1],x+30,y+102,{size:22,w:400}); txt(c,cd[2],x+30,y+132,{size:18,col:C.dim});
    const gx=x+30, gy=y+158, gw=w-60, gh=110; plotFrame(c,gx,gy,gw,gh); c.save(); c.beginPath(); c.rect(gx,gy,gw,gh); c.clip(); const pts=[];
    for(let j=0;j<=200;j++){ const u=j/200; const e=sstep(.5-cd[3]-.02,.5-cd[3]+.02,u)*(1-sstep(.5+cd[3]-.02,.5+cd[3]+.02,u)); let v=.1+e*(.52+vnoise(u*120+bt*12)*.05)+vnoise(u*50+bt*3)*.02; if(cd[4]==='vsb') v+=Math.exp(-Math.pow((u-(.5-cd[3]+.012))/.008,2))*.3; pts.push([gx+u*gw,gy+gh-clamp(v)*gh*1.2]); }
    c.strokeStyle=on?PAL.accent:'rgba(92,168,219,.5)'; c.lineWidth=2.2; if(on){c.shadowColor=PAL.accent;c.shadowBlur=12;} c.beginPath(); pts.forEach((p,j)=>j?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke(); c.restore();
    const ok=on||(sel>i); txt(c,ok?'●  detected':'○  looking',x+30,y+h-18,{size:18,w:500,col:ok?PAL.ok:C.dim2,mono:true}); c.restore(); });
  const k=eo5(seg(bt,3.8,4.6)); if(k>0){ c.save(); c.globalAlpha=k; const x=110,y=740,w=1700,h=220; txt(c,'Channel scanner',x,y-64,{size:28,w:600,col:C.ink}); txt(c,'470 – 694 MHz',x+w,y-64,{size:20,mono:true,col:C.dim,al:'right'});
    const muxes=[[.12,'474'],[.38,'522'],[.62,'570'],[.86,'618']]; const sw=seg(bt,4.4,6.8);
    plotFrame(c,x,y,w,h); c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip(); const pts=[]; for(let i=0;i<=500;i++){ const u=i/500; let d=floorDb(u,bt); for(const m of muxes){ if(sw>m[0]-.01) d=Math.max(d,ofdm(u,bt,m[0],.035)); } pts.push([x+u*w,y+h-clamp((d+100)/68)*h]); }
    // radar-style sweep trail
    const tr=c.createLinearGradient(x+w*sw-w*.12,0,x+w*sw,0); tr.addColorStop(0,'rgba(92,168,219,0)'); tr.addColorStop(1,'rgba(92,168,219,.28)'); c.fillStyle=tr; c.fillRect(x+w*Math.max(0,sw-.12),y,w*Math.min(.12,sw),h);
    c.strokeStyle=PAL.accent; c.lineWidth=2.2; c.shadowColor=PAL.accent; c.shadowBlur=10; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke(); c.shadowBlur=0;
    c.strokeStyle='rgba(220,240,255,.8)'; c.lineWidth=2; c.beginPath(); c.moveTo(x+w*sw,y); c.lineTo(x+w*sw,y+h); c.stroke(); c.restore();
    muxes.forEach(([u,l],i)=>{ const a=eoBack(seg(bt,5+i*.5,5.6+i*.5)); if(a<=0) return; c.save(); c.globalAlpha=clamp(a); const px=x+u*w, pu=1+.08*pop(bt,5+i*.5); c.translate(px,y-14); c.scale(pu,pu); c.strokeStyle=PAL.ok; c.lineWidth=2; c.beginPath(); c.moveTo(0,14+h*.2); c.lineTo(0,6); c.stroke(); txt(c,l+' MHz',0,0,{size:20,w:600,mono:true,al:'center',col:PAL.ok}); c.restore(); });
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
  const tk=eoBack(seg(bt,1.2,2.2)); c.save(); c.globalAlpha=clamp(tk); c.translate((1-clamp(tk))*70,0);
  const tx=1280,ty=290,tw=530,th=596; glass(c,tx-14,ty-14,tw+28,th+28,26,{blur:40}); c.save(); rr(c,tx,ty,tw,th,12); c.clip(); c.fillStyle='#000'; c.fillRect(tx,ty,tw,th);
  const row=(n,s,col,bgc)=>{ const yy=ty+22+n*27; if(bgc){c.fillStyle=bgc;c.fillRect(tx+14,yy-21,tw-28,27);} txt(c,s,tx+22,yy,{size:21,mono:true,w:500,col}); };
  row(0,'P100  OnAir Text   18:04','#fff','#0000c8'); row(2,'NEWS','#ffeb3b'); row(3,'101 Headlines','#00e5ff'); row(4,'110 Politics','#00e5ff'); row(5,'120 World','#00e5ff'); row(7,'SPORT','#ffeb3b'); row(8,'200 Scores','#00e5ff'); row(9,'210 Fixtures','#00e5ff'); row(11,'WEATHER','#ffeb3b'); row(12,'300 Today','#00e5ff'); row(13,'310 Week ahead','#00e5ff');
  [['#e53935','NEWS'],['#43a047','SPORT'],['#fdd835','WEATHER'],['#00acc1','TV']].forEach(([cc,l],i)=>{ c.fillStyle=cc; c.fillRect(tx+14+i*(tw-28)/4,ty+th-60,(tw-28)/4-6,34); txt(c,l,tx+14+i*(tw-28)/4+((tw-28)/4-6)/2,ty+th-36,{size:17,mono:true,w:700,al:'center',col:'#000'}); });
  c.fillStyle='rgba(0,0,0,.18)'; for(let yy=0;yy<th;yy+=4) c.fillRect(tx,ty+yy+2,tw,2); c.restore(); c.restore();
  ['Live video + audio','Programme guide','DVB subtitles','Teletext'].forEach((s,i)=>chip(c,s,[310,720,1130,1480][i],985,eoBack(seg(bt,1.5+i*.5,2.1+i*.5)),{size:24,pop:pop(bt,1.5+i*.5)}));
  c.restore();
  const q=eo5(seg(bt,3.4,4.2)); if(q>0){ c.save(); c.globalAlpha=q; txt(c,'Signal quality',1280,236,{size:20,col:C.dim}); rr(c,1280,246,530,16,8); c.fillStyle='#14181c'; c.fill(); const qq=eo3(seg(bt,3.5,5.0)); rr(c,1280,246,Math.max(16,530*qq),16,8); const gq=c.createLinearGradient(1280,0,1810,0); gq.addColorStop(0,PAL.bad); gq.addColorStop(.5,PAL.warn); gq.addColorStop(1,PAL.ok); c.fillStyle=gq; c.shadowColor=PAL.ok; c.shadowBlur=14*qq; c.fill(); c.shadowBlur=0; txt(c,'100% excellent',1810,236,{size:20,w:600,mono:true,al:'right',col:PAL.ok}); c.restore(); }
};
// ---------------------------------------------------------------- every standard it decodes
const STD=[['DVB-T2','T2-Base and T2-Lite',['1.7 – 10 MHz channels','FFT 1K to 32K','QPSK to 256-QAM','Pilot patterns PP1–PP8']],
           ['DVB-T','First-generation terrestrial',['2K and 8K carriers','QPSK, 16- and 64-QAM','All guard intervals','All code rates']],
           ['ATSC 1.0','North American 8-VSB',['6 MHz channels','Adaptive equaliser (DFE)','Trellis + Reed-Solomon','PSIP channel names']]];
T.standards=(c,bt)=>{
  bgd(c,.9);
  c.save(); c.globalAlpha=.22; ridge(c,0,700,W,420,bt+6,[{u:.5,w:.3,k:1}],{rows:20,n:140,dt:.25}); c.restore();
  head(c,'Every standard.','Decoded in software.',bt,.2,{size:66,y:150});
  STD.forEach((cd,i)=>{ const x=110+i*600, y=330, w=560, h=470, k0=1.5+i, k=eoBack(seg(bt,k0,k0+.8)); if(k<=0) return;
    c.save(); c.globalAlpha=clamp(k); c.translate(x+w/2,y+h/2+(1-clamp(k))*60); const pu=1+.015*pop(bt,k0); c.scale(pu,pu); c.translate(-(x+w/2),-(y+h/2));
    glass(c,x,y,w,h,24,{active:true}); sheen(c,cd[0],x+40,y+96,bt,k0+.1,{size:68,ls:-2}); txt(c,cd[1],x+42,y+138,{size:24,w:400,col:C.dim});
    cd[2].forEach((it,j)=>{ const a=eo5(seg(bt,k0+.4+j*.12,k0+.9+j*.12)); const yy=y+206+j*58; c.save(); c.globalAlpha=a; c.translate((1-a)*24,0); c.fillStyle=acc(.18); c.beginPath(); c.arc(x+56,yy-8,15,0,7); c.fill(); c.strokeStyle=PAL.accent; c.lineWidth=3; c.lineCap='round'; c.beginPath(); c.moveTo(x+49,yy-8); c.lineTo(x+54,yy-2); c.lineTo(x+64,yy-15); c.stroke(); txt(c,it,x+86,yy,{size:27,w:500}); c.restore(); });
    c.restore(); });
  [['Automatic standard detection',4.0],['Automatic bandwidth',4.5],['Transport stream out',5.0]].forEach(([s,t0],i)=>chip(c,s,[420,960,1450][i],900,eoBack(seg(bt,t0,t0+.6)),{size:26,pop:pop(bt,t0)}));
};
// ---------------------------------------------------------------- built to be exact
T.exact=(c,bt)=>{
  bgd(c,.9);
  head(c,'Built to be exact.',null,bt,.2,{size:66,y:150});
  reveal(c,'A receiver you can trust, because it is checked against the reference.',110,214,bt,.6,{size:30,w:400,col:C.dim,ls:-.3});
  const cards=[['Bit-exact','Coding chains checked against GNU Radio’s reference encoders.'],['Live-tested','Locked on a real DVB-T2 multiplex at 522 MHz.'],['Real-time','LDPC on the GPU with Metal, or SIMD on the CPU.']];
  cards.forEach((cd,i)=>{ const x=110+i*600, y=310, w=560, h=560, k=eoBack(seg(bt,1.0+i*1.0,1.8+i*1.0)); if(k<=0) return; const pu=1+.015*pop(bt,1+i);
    c.save(); c.globalAlpha=clamp(k); c.translate(x+w/2,y+h/2+(1-clamp(k))*60); c.scale(pu,pu); c.translate(-(x+w/2),-(y+h/2));
    glass(c,x,y,w,h,26,{active:i===0}); bigNum(c,cd[0],x+40,y+120,92,1); c.save(); c.font='400 28px Inter'; wrapText(c,cd[1],x+42,y+176,w-84,38,C.dim); c.restore();
    const gx=x+40,gy=y+260,gw=w-80,gh=h-300;
    if(i===0){ for(let r=0;r<5;r++){ const yy=gy+6+r*38; for(let q=0;q<9;q++){ const v=((hash(r*13+q*5)*255)|0).toString(16).padStart(2,'0').toUpperCase(); const scn=seg(bt,2.0,3.0)*10-r*1.6; const hl=clamp(scn)*(1-clamp(scn-1.2)); c.save(); if(hl>0){ c.fillStyle=acc(.22*hl); c.fillRect(gx+q*50-4,yy+2,44,30); } c.restore(); txt(c,v,gx+q*50,yy+24,{size:24,mono:true,col:scn>.5?PAL.ok:C.dim}); } }
      const ck=eoBack(seg(bt,2.6,3.4)); c.save(); c.globalAlpha=clamp(ck); c.translate(gx+gw-30,gy+gh-34); c.scale(clamp(ck,0,1.3),clamp(ck,0,1.3)); c.fillStyle=PAL.ok; c.shadowColor=PAL.ok; c.shadowBlur=24; c.beginPath(); c.arc(0,0,30,0,7); c.fill(); c.shadowBlur=0; c.strokeStyle='#04140b'; c.lineWidth=7; c.lineCap='round'; c.beginPath(); c.moveTo(-14,1); c.lineTo(-3,12); c.lineTo(15,-12); c.stroke(); c.restore(); txt(c,'identical',gx,gy+gh-18,{size:26,w:600,col:PAL.ok}); }
    else if(i===1){ plotFrame(c,gx,gy,gw,gh*.75); const sg=[{u:.5,w:.3,k:1}]; c.save(); c.beginPath(); c.rect(gx,gy,gw,gh*.75); c.clip(); c.strokeStyle=PAL.accent; c.lineWidth=2.4; c.shadowColor=PAL.accent; c.shadowBlur=10; c.beginPath(); for(let j=0;j<=240;j++){ const u=j/240, d=specDb(u,bt+3,sg); const px=gx+u*gw, py=gy+gh*.75-clamp((d+100)/68)*gh*.75; j?c.lineTo(px,py):c.moveTo(px,py);} c.stroke(); c.restore(); txt(c,'522.000 MHz',gx,gy+gh-8,{size:26,mono:true,w:500,col:PAL.accent}); }
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
  c.strokeStyle=PAL.accent; c.lineWidth=2.8; c.shadowColor=PAL.accent; c.shadowBlur=14; c.beginPath(); pts.forEach((p,i)=>i?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.stroke(); c.restore();
  echoes.forEach(([d0,db],i)=>{ const kk=eo5(seg(bt,1.5+i*.5,2.1+i*.5)); if(i>0&&kk>.5) txt(c,db+' dB',ux(d0),base-Math.pow(10,db/20)*scl-18,{size:20,w:600,mono:true,al:'center',col:i<3?PAL.warn:PAL.bad,a:kk}); });
  const b=eoBack(seg(bt,3.3,3.9)); if(b>0){ c.save(); c.globalAlpha*=clamp(b); c.translate(1450,377); c.scale(clamp(b,0,1.15),clamp(b,0,1.15)); glass(c,-200,-47,400,94,16,{blur:30,top:'#2a2210',bot:'#17120a'}); c.strokeStyle=PAL.warn; c.lineWidth=2; rr(c,-199,-46,398,92,16); c.stroke(); txt(c,'multipath: likely',-172,-5,{size:32,w:600,col:PAL.warn}); txt(c,'echo −7 dB at +1.1 µs',-172,29,{size:19,mono:true,col:C.dim}); c.restore(); }
  c.restore();
  c.save(); c.globalAlpha=sw; c.translate((1-sw)*80,0);
  kinetic(c,'…and where to point',110,430,bt,5.0,{size:70,w:500,ls:-2.5,stagger:.02}); kinetic(c,'the antenna.',110,515,bt,5.25,{size:70,w:500,ls:-2.5,col:PAL.accent,stagger:.02}); reveal(c,'It measures every direction and tells you.',110,580,bt,5.7,{size:30,w:400,col:C.dim,ls:-.4});
  const cx=1390, cy=540, R=330; const gl=c.createRadialGradient(cx,cy,0,cx,cy,R*1.3); gl.addColorStop(0,'rgba(92,168,219,.12)'); gl.addColorStop(1,'rgba(92,168,219,0)'); c.fillStyle=gl; c.fillRect(cx-R*1.4,cy-R*1.4,R*2.8,R*2.8);
  c.strokeStyle='#262b31'; c.lineWidth=1.6; for(const r of [R,R*.66,R*.33]){ c.beginPath(); c.arc(cx,cy,r,0,7); c.stroke(); }
  const swp=seg(bt,5.0,8.6); if(swp<1){ const ang=-Math.PI/2+swp*Math.PI*2*.9; const cg=c.createConicGradient(ang-1.1,cx,cy); cg.addColorStop(0,'rgba(92,168,219,0)'); cg.addColorStop(.17,'rgba(92,168,219,.30)'); cg.addColorStop(.175,'rgba(92,168,219,0)'); cg.addColorStop(1,'rgba(92,168,219,0)'); c.fillStyle=cg; c.beginPath(); c.arc(cx,cy,R*1.02,0,7); c.fill(); c.strokeStyle='rgba(210,235,250,.7)'; c.lineWidth=2; c.beginPath(); c.moveTo(cx,cy); c.lineTo(cx+Math.cos(ang)*R*1.02,cy+Math.sin(ang)*R*1.02); c.stroke(); }
  const dirs=['N','NE','E','SE','S','SW','W','NW'], score=[.38,.86,.52,.28,.22,.30,.41,.63];
  dirs.forEach((dn,i)=>{ const a=-Math.PI/2+i*Math.PI/4, rch=seg(bt,5.0+i*.5,5.6+i*.5), best=i===1&&bt>8.5; const len=R*score[i]*eo5(rch);
    c.save(); c.strokeStyle=best?PAL.ok:PAL.accent; c.lineWidth=best?18:13; c.lineCap='round'; c.shadowColor=best?PAL.ok:PAL.accent; c.shadowBlur=18*rch; c.beginPath(); c.moveTo(cx+Math.cos(a)*44,cy+Math.sin(a)*44); c.lineTo(cx+Math.cos(a)*(44+len*(R-44)/R),cy+Math.sin(a)*(44+len*(R-44)/R)); c.stroke(); c.restore();
    txt(c,dn,cx+Math.cos(a)*(R+36),cy+Math.sin(a)*(R+36)+9,{size:24,w:600,al:'center',col:best?PAL.ok:C.dim});
    if(rch>.9) txt(c,Math.round(score[i]*100)+'',cx+Math.cos(a)*(44+len*(R-44)/R+26),cy+Math.sin(a)*(44+len*(R-44)/R+26)+6,{size:17,mono:true,al:'center',col:best?PAL.ok:C.ink}); });
  c.fillStyle='#141a20'; c.beginPath(); c.arc(cx,cy,40,0,7); c.fill(); c.strokeStyle='#39424a'; c.stroke();
  const fin=eoBack(seg(bt,8.5,9.2)); if(fin>0){ c.save(); c.globalAlpha*=clamp(fin); c.translate(390,683); c.scale(clamp(fin,0,1.1),clamp(fin,0,1.1)); glass(c,-280,-43,560,86,16,{blur:30,top:'#13261b',bot:'#0b160f'}); c.strokeStyle=PAL.ok; c.lineWidth=2; rr(c,-279,-42,558,84,16); c.stroke(); txt(c,'Point it north-east.',-250,13,{size:38,w:600,col:PAL.ok}); c.restore(); }
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
  outs.forEach(([a,b,yy],i)=>{ const a2=eoBack(seg(bt,1.5+i*.5,2.2+i*.5)); c.save(); c.globalAlpha=k*clamp(a2); c.translate((1-clamp(a2))*50,0); node(ox,yy-60,450,126,a,b,false); c.restore();
    c.strokeStyle=acc(.4*clamp(a2)); c.lineWidth=2; c.beginPath(); c.moveTo(530,605); c.bezierCurveTo(800,605,820,yy+3,ox,yy+3); c.stroke();
    for(let p=0;p<6;p++){ const u=((bt*.5+p/6+i*.13)%1); const xx=bez(530,800,820,ox,u), yy2=bez(605,605,yy+3,yy+3,u); c.save(); c.fillStyle='#bfe3fa'; c.shadowColor=PAL.accent; c.shadowBlur=18; c.globalAlpha=k*clamp(a2)*Math.sin(u*Math.PI); c.beginPath(); c.arc(xx,yy2,5.5,0,7); c.fill(); c.restore(); } });
  ['VLC','ffplay','tvheadend'].forEach((n,i)=>chip(c,n,1700,545+i*60,eoBack(seg(bt,3.0+i*.3,3.6+i*.3))*k,{size:21,pad:36}));
  c.restore();
};
// ---------------------------------------------------------------- outro
T.outro=(c,bt)=>{
  bgd(c,1);
  const lk=eoBack(seg(bt,.2,1.0))*(1+.025*bp(bt)*seg(bt,1,1.5)); c.save(); c.translate(W/2,262); c.scale(lk,lk);
  const gl=c.createRadialGradient(0,0,0,0,0,360); gl.addColorStop(0,'rgba(224,97,87,.22)'); gl.addColorStop(1,'rgba(224,97,87,0)'); c.fillStyle=gl; c.fillRect(-400,-400,800,800);
  c.shadowColor='rgba(0,0,0,.6)'; c.shadowBlur=60; c.shadowOffsetY=24; c.globalAlpha=seg(bt,.2,.6); c.drawImage(logo,-160,-160,320,320); c.restore();
  burst(c,W/2,262,bt,.2,70); shock(c,W/2,262,.2,bt);
  sheen(c,'OnAir',W/2,595,bt,1.0,{size:150,al:'center',ls:-6});
  kinetic(c,'Open-source digital TV receiver for your SDR.',W/2,665,bt,1.5,{size:40,w:400,col:C.dim,ls:-.8,stagger:.015,al:'center'});
  const row1=['DVB-T2','T2-Lite','DVB-T','ATSC'], w1=row1.map(s=>s.length*14+64), t1=w1.reduce((a,b)=>a+b+16,-16); let cx=W/2-t1/2;
  row1.forEach((s,i)=>{ chip(c,s,cx+w1[i]/2,740,eoBack(seg(bt,2.0+i*.5,2.6+i*.5)),{size:22,pad:40,pop:pop(bt,2+i*.5)}); cx+=w1[i]+16; });
  const k=eoBack(seg(bt,4.0,4.8)); c.save(); c.globalAlpha=clamp(k); c.translate(0,(1-clamp(k))*20);
  const plat=['macOS','Linux']; let px=W/2-(200+200+16)/2;
  plat.forEach((pl,i)=>{ glass(c,px,810,200,62,31,{active:true,blur:24,dy:8}); txt(c,pl,px+100,850,{size:28,w:600,al:'center'}); px+=216; });
  txt(c,'Runs natively on macOS and Linux*',W/2,920,{size:26,w:500,al:'center',col:C.ink}); c.restore();
  reveal(c,'github.com/adswill/OnAir',W/2,985,bt,5.0,{size:30,w:500,col:PAL.accent,mono:true,al:'center'});
  reveal(c,'* Windows coming soon.',W/2,1038,bt,5.5,{size:22,w:400,col:C.dim,al:'center'});
};

// ---------------------------------------------------------------- timeline: scene, bar-line start, bar-line end, transition into it, overlap
const B=[['hook',0,6,null,0],['logo',6,12,'zoom',.7],['radios',12,20,'iris',.8],['auto',20,28,'sweep',.8],['live',28,36,'push',.7],['standards',36,44,'zoom',.7],['exact',44,50,'slices',.7],['see',50,58,'iris',.8],['fix',58,68,'sweep',.8],['out',68,74,'push',.7],['outro',74,82,'zoom',.9]];
const TAG={radios:'ANY RADIO',auto:'AUTOMATIC',live:'LIVE TV',standards:'STANDARDS',exact:'EXACT',see:'SIGNAL',fix:'RECEPTION',out:'OUTPUT'};
const SCENES=B.map((s,i)=>{ const next=B[i+1]; const t0=s[1]-(s[4]||0)/2; const t1=s[2]+(next?next[4]/2:0); const o=[s[0],t1-t0,s[3],s[4]||0]; o.t0=t0; o.t1=t1; o.start=s[1]; o.idx=i; return o; });
const TOTAL=82; window.TOTAL=TOTAL; window.NF=Math.round(TOTAL*FPS);
window.SCENES=SCENES.map(s=>({name:s[0],t0:s.t0,t1:s.t1,tr:s[2],trd:s[3]}));
const bufA=mk(), bufB=mk(), bufT=mk(), ca=bufA.getContext('2d'), cb=bufB.getContext('2d'), ct=bufT.getContext('2d');
function sceneAt(c,s,t){ c.clearRect(0,0,W,H); const bt=t-s.start; CURBT=bt; CURT=t; c.save();
  if(s[0]!=='hook'){ const z=1+.0042*clamp(bt+.6,0,12); c.translate(W/2+Math.sin(bt*.35)*6,H/2+Math.cos(bt*.3)*4); c.scale(z,z); c.translate(-W/2,-H/2); }
  T[s[0]](c,bt); c.restore();
  if(TAG[s[0]]){ const a=eo3(seg(bt,.5,1.1)); c.save(); c.globalAlpha=a*.8; txt(c,String(s.idx-1).padStart(2,'0')+'  ·  '+TAG[s[0]],W-110,84,{size:18,mono:true,w:500,col:C.dim,al:'right',ls:3}); c.restore(); } }
function trans(dst,type,p){
  const e=eio(p); dst.fillStyle='#000'; dst.fillRect(0,0,W,H);
  if(type==='iris'){ dst.drawImage(bufA,0,0); dst.save(); const r=e*Math.hypot(W,H)*.75, ox=W/2, oy=H*.42; dst.beginPath(); dst.arc(ox,oy,r,0,7); dst.clip(); dst.drawImage(bufB,0,0); dst.restore();
    dst.save(); dst.strokeStyle='rgba(190,225,245,.95)'; dst.globalAlpha=(1-e)*.95; dst.lineWidth=4; dst.shadowColor=PAL.accent; dst.shadowBlur=40; dst.beginPath(); dst.arc(ox,oy,r,0,7); dst.stroke(); dst.restore(); }
  else if(type==='sweep'){ dst.drawImage(bufA,0,0); const x=lerp(-300,W+300,e), sk=240; dst.save(); dst.beginPath(); dst.moveTo(0,0); dst.lineTo(x+sk,0); dst.lineTo(x-sk,H); dst.lineTo(0,H); dst.closePath(); dst.clip(); dst.drawImage(bufB,0,0); dst.restore();
    dst.save(); const g=dst.createLinearGradient(x-120,0,x+120,0); g.addColorStop(0,'rgba(92,168,219,0)'); g.addColorStop(.5,'rgba(205,232,250,.7)'); g.addColorStop(1,'rgba(92,168,219,0)'); dst.fillStyle=g; dst.globalAlpha=Math.sin(p*Math.PI); dst.beginPath(); dst.moveTo(x+sk-120,0); dst.lineTo(x+sk+120,0); dst.lineTo(x-sk+120,H); dst.lineTo(x-sk-120,H); dst.closePath(); dst.fill(); dst.restore(); }
  else if(type==='zoom'){ dst.save(); dst.globalAlpha=1-eo3(p); const s1=1+e*.4; dst.translate(W/2,H/2); dst.scale(s1,s1); dst.translate(-W/2,-H/2); dst.drawImage(bufA,0,0); dst.restore();
    dst.save(); dst.globalAlpha=eo3(p); const s2=1.22-e*.22; dst.translate(W/2,H/2); dst.scale(s2,s2); dst.translate(-W/2,-H/2); dst.drawImage(bufB,0,0); dst.restore(); }
  else if(type==='push'){ dst.save(); dst.translate(-e*W*.45,0); dst.globalAlpha=1-e*.8; dst.drawImage(bufA,0,0); dst.restore(); dst.save(); dst.translate((1-e)*W,0); dst.drawImage(bufB,0,0); dst.restore(); }
  else if(type==='slices'){ dst.drawImage(bufA,0,0); const n=12, sw=W/n; for(let i=0;i<n;i++){ const qq=eo3(clamp(p*1.5-(i/n)*.5)); dst.save(); dst.beginPath(); dst.rect(i*sw,H*(1-qq),sw+1,H*qq); dst.clip(); dst.drawImage(bufB,0,0); dst.restore(); } }
  else { dst.drawImage(bufA,0,0); dst.save(); dst.globalAlpha=e; dst.drawImage(bufB,0,0); dst.restore(); }
}
function composite(t){
  const act=SCENES.filter(s=>t>=s.t0&&t<s.t1);
  if(act.length===1){ sceneAt(ctx,act[0],t); window.CA=0; return; }
  const A=act[0],Bn=act[1], p=clamp((t-Bn.t0)/Bn[3]); sceneAt(ca,A,t); sceneAt(cb,Bn,t);
  // motion blur: average three sub-positions of the transition
  const ps=[p,clamp(p-.045),clamp(p-.09)]; ctx.clearRect(0,0,W,H);
  ps.forEach((pp,i)=>{ trans(ct,Bn[2],pp); ctx.globalAlpha=1/(i+1); ctx.drawImage(bufT,0,0); }); ctx.globalAlpha=1;
  window.CA=Math.sin(p*Math.PI);
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
  ctx.save(); ctx.globalCompositeOperation='lighter'; ctx.globalAlpha=.42; ctx.imageSmoothingQuality='high'; ctx.drawImage(bl2,0,0,W,H); ctx.restore();
  // vignette
  const g=ctx.createRadialGradient(W/2,H/2,H*.45,W/2,H/2,H*1.0); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.45)'); ctx.fillStyle=g; ctx.fillRect(0,0,W,H);
  // film grain
  ctx.save(); ctx.globalAlpha=.045; ctx.globalCompositeOperation='overlay'; const ox=(hash(frame*1.7)*256)|0, oy=(hash(frame*2.3)*256)|0; for(let x=-ox;x<W;x+=256) for(let y=-oy;y<H;y+=256) ctx.drawImage(grain,x,y); ctx.restore();
  const o=seg(t,TOTAL-0.9,TOTAL); if(o>0){ctx.fillStyle=`rgba(0,0,0,${o})`;ctx.fillRect(0,0,W,H)}
}
window.ready=Promise.all([document.fonts.load('500 20px Inter'),document.fonts.load('600 20px Inter'),document.fonts.load('400 20px Inter'),document.fonts.load('700 20px Inter'),document.fonts.load('500 20px "JetBrains Mono"'),logo.decode()]);
window.renderFrame=async(i)=>{ const t=i/FPS; composite(t); post(t,i); return t; };
