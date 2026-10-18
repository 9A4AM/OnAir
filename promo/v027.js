// OnAir 0.2.7: what changed since 0.2.4, cut to a rock song (the song file is never in the repository; see footage/README.md).
// The film is built on the song's own audio (audio028.js, from footage/audio_features_028.py): its chapters start on the song's real section boundaries,
// the cuts, flashes and shakes fire on its real kick and snare hits (with their strength), and OnAir "receives" the song: its live spectrum and waterfall
// run along the bottom of the film. Modes and groups are the app's own mode table (groups027.js); footage is the 0.2.7 build on each mode's built-in test
// signal (clips7: n_<id> new modes, o_<id> older ones, f_<name> features). 60 fps.
const RED='#ff5a46', V='0.2.7', FPS7=60;
const logo7=new Image(); logo7.src='assets/logo_027.png';
// ---------------------------------------------------------------- audio: events, section times, the song's spectrum
const NBND=AUD.nb, ABAND=Uint8Array.from(atob(AUD.bands),c=>c.charCodeAt(0)), ALOUD=Uint8Array.from(atob(AUD.loud),c=>c.charCodeAt(0));
const KICK=AUD.kick, SNARE=AUD.snare, HAT=AUD.hat, SEC=AUD.sections, BAND=AUD.band, END=AUD.final, LEN=AUD.len+1.4;
const [Q0,Q1,Q2,Q3,Q4,Q5,Q6,Q7,Q8,Q9]=SEC;
const bandAt=(t,k)=>{ const i=Math.round(t*AUD.fps); return i<0||i>=AUD.nf?0:ABAND[i*NBND+k]/255; };
const loudAt=t=>ALOUD[clamp(Math.round(t*AUD.fps),0,AUD.nf-1)]/255;
function lastEv(arr,t){ let lo=0,hi=arr.length-1,r=-1; while(lo<=hi){ const m=(lo+hi)>>1; if(arr[m][0]<=t){ r=m; lo=m+1; } else hi=m-1; } return r; }
function envEv(arr,t,decay){ let v=0; const i=lastEv(arr,t); for(let k=0;k<3&&i-k>=0;k++){ const [te,s]=arr[i-k]; v=Math.max(v,s*Math.exp(-(t-te)*decay)); } return v; }
const KE=t=>envEv(KICK,t,10), SE=t=>envEv(SNARE,t,8), HE=t=>envEv(HAT,t,14), BE=t=>Math.max(KE(t),SE(t));
function nearEv(arr,t,minS=0,maxD=.4){ let best=null,bd=9; const i=lastEv(arr,t); for(let k=Math.max(0,i-8);k<Math.min(arr.length,i+9);k++){ const [te,s]=arr[k]; if(s<minS) continue; const d=Math.abs(te-t); if(d<bd){ bd=d; best=te; } } return best!=null&&bd<=maxD?best:t; }
const evIn=(arr,a,b,minS=0)=>arr.filter(e=>e[0]>=a&&e[0]<b&&e[1]>=minS).map(e=>e[0]);
const ALLEV=KICK.concat(SNARE).sort((a,b)=>a[0]-b[0]);
const hitNow=(t,list)=>{ let k=0; for(const h of list) k=Math.max(k,t<h?0:Math.exp(-(t-h)*6)); return k; };
// ---------------------------------------------------------------- the song as a received signal: a live waterfall (Turbo, like the app) and spectrum
const turbo=v=>{ v=clamp(v); const r=34.61+v*(1172.33-v*(10793.56-v*(33300.12-v*(38394.49-v*14825.05)))), g=23.31+v*(557.33+v*(1225.33-v*(3574.96-v*(1073.77+v*707.56)))), b=27.2+v*(3211.1-v*(15327.97-v*(27814-v*(22569.18-v*6838.66)))); return [clamp(r,0,255),clamp(g,0,255),clamp(b,0,255)]; };
const TURBO=Array.from({length:256},(_,i)=>turbo(i/255));
const SWF=mk(NBND,56), swfx=SWF.getContext('2d'), swfd=swfx.createImageData(NBND,56);
function songWF(t){ const d=swfd.data; for(let r=0;r<56;r++){ const tt=t-r*.03; for(let k=0;k<NBND;k++){ const v=bandAt(tt,k), [R,G,B]=TURBO[Math.min(255,Math.floor(Math.pow(v,1.15)*255))]; const o=(r*NBND+k)*4; d[o]=R; d[o+1]=G; d[o+2]=B; d[o+3]=255; } } swfx.putImageData(swfd,0,0); }
function songSpec(c,x,y,w,h,t,o={}){ const pk=[]; for(let k=0;k<NBND;k++){ let m=0; for(let j=0;j<7;j++) m=Math.max(m,bandAt(t-j*.06,k)); pk.push(m); }
  c.save(); c.beginPath(); c.rect(x,y,w,h); c.clip(); c.lineJoin='round';
  c.strokeStyle='rgba(255,170,90,.55)'; c.lineWidth=1.6; c.beginPath(); for(let k=0;k<NBND;k++){ const px=x+k*w/(NBND-1), py=y+h-pk[k]*h*.94; k?c.lineTo(px,py):c.moveTo(px,py); } c.stroke();
  const g=c.createLinearGradient(0,y,0,y+h); g.addColorStop(0,o.col||'rgba(92,168,219,.55)'); g.addColorStop(1,'rgba(92,168,219,0)'); c.fillStyle=g; c.beginPath(); c.moveTo(x,y+h); for(let k=0;k<NBND;k++){ c.lineTo(x+k*w/(NBND-1),y+h-bandAt(t,k)*h*.94); } c.lineTo(x+w,y+h); c.closePath(); c.fill();
  c.strokeStyle='#fff'; c.lineWidth=o.lw||2.2; c.beginPath(); for(let k=0;k<NBND;k++){ const px=x+k*w/(NBND-1), py=y+h-bandAt(t,k)*h*.94; k?c.lineTo(px,py):c.moveTo(px,py); } c.stroke(); c.restore(); }
function ambient(c,t,a,tint){ c.save(); c.globalAlpha*=a*.34; c.imageSmoothingEnabled=true; c.imageSmoothingQuality='high'; c.filter='blur(26px) saturate(.8)'; c.drawImage(SWF,0,0,NBND,56,-40,-40,W+80,H+80); c.restore();
  if(tint){ c.save(); c.globalCompositeOperation='multiply'; c.fillStyle=tint; c.fillRect(0,0,W,H); c.restore(); } }
function hud(c,t,a=1){ const h=118, y=H-h; c.save(); c.globalAlpha*=a; c.fillStyle='rgba(4,6,9,.82)'; c.fillRect(0,y,W,h); c.fillStyle='rgba(255,255,255,.1)'; c.fillRect(0,y,W,1);
  c.save(); c.imageSmoothingEnabled=true; c.beginPath(); c.rect(0,y+38,W,h-38); c.clip(); c.globalAlpha*=.95; c.drawImage(SWF,0,0,NBND,56,0,y+38,W,h-38); c.restore(); songSpec(c,0,y+38,W,h-38,t,{lw:2});
  const k=KE(t); c.fillStyle=`rgba(255,90,70,${.5+.5*k})`; c.beginPath(); c.arc(34,y+19,6+3*k,0,7); c.fill();
  txt(c,'RECEIVING',54,y+26,{size:19,w:800,col:'#ff8c7d',ls:4}); txt(c,'Black Sabbath · Paranoid',200,y+26,{size:19,w:600,col:'#c8d2dc'}); txt(c,'AUDIO  50 Hz – 14 kHz',W-30,y+26,{size:17,w:600,col:'#7f8a96',al:'right',ls:2});
  c.restore(); }
// ---------------------------------------------------------------- footage helpers
const c7=(n,t)=>'file:///tmp/clips7/'+n+'/f_'+pad(clamp(Math.floor(t*30),0,(window.C7N[n]||240)-1)+1,5)+'.jpg';
const CROPD={cx:1112,cy:575,z:1.19}, crop7=n=>(window.CROPS7&&window.CROPS7[n])||CROPD, has7=n=>!!(window.C7N&&window.C7N[n]);
const loopT=(n,t,a=.6)=>{ const dur=(window.C7N[n]||90)/30; return a+(((t%Math.max(.5,dur-a-.4))+Math.max(.5,dur-a-.4))%Math.max(.5,dur-a-.4)); };
function tex7(buf,n,t,cm){ return tex(buf,x=>foot(x,c7(n,t),cm||crop7(n))); }
function shake(t,amt){ const k=BE(t)*amt; return [(hash(Math.floor(t*60)*1.3)-.5)*k,(hash(Math.floor(t*60)*2.1)-.5)*k]; }
function flash(c,k,col='255,255,255'){ if(k<=.01) return; c.save(); c.globalCompositeOperation='lighter'; c.fillStyle=`rgba(${col},${Math.min(1,k)})`; c.fillRect(0,0,W,H); c.restore(); }
function kicker(c,s,x,y,a=1,col=RED,al='left',size=26){ if(a<=0) return; txt(c,s,x,y,{size,w:800,col,ls:7,a,al}); }
const rgba=(col,a)=>col.replace('rgb','rgba').replace(')',','+a+')');
function ring(c,x,y,t,t0,col='255,120,100',R=900){ const k=seg(t,t0,t0+.7); if(k<=0||k>=1) return; c.save(); c.globalCompositeOperation='lighter'; c.strokeStyle=`rgba(${col},${.6*(1-k)})`; c.lineWidth=2+10*(1-k); c.beginPath(); c.arc(x,y,eio3(k)*R,0,7); c.fill&&0; c.stroke(); c.restore(); }
const MODES=GROUPS.flatMap((g,gi)=>g.members.map(m=>({...m,g:gi,col:g.col,clip:(m.new?'n_':'o_')+m.id})));
const NEWM=MODES.filter(m=>m.new), clipOf=m=>has7(m.clip)?m.clip:null;
const eob=k=>{ k=clamp(k); const c1=1.70158, c3=c1+1; return 1+c3*Math.pow(k-1,3)+c1*Math.pow(k-1,2); };
// ---------------------------------------------------------------- 0. cold open: tuning in. 0.2.4 rolls to 0.2.5 and 0.2.6 on the first taps, then the lock closes on 0.2.7 as the band comes in
const RV=[[0,'0.2.4'],[nearEv(ALLEV,1.9,.2,.6),'0.2.5'],[nearEv(ALLEV,3.5,.2,.6),'0.2.6'],[Q0,'0.2.7']];
const RVN={'0.2.5':'86 changes','0.2.6':'202 changes'};
function sIntro(c,t){
  c.fillStyle='#030405'; c.fillRect(0,0,W,H); const lock=seg(t,Q0-1.7,Q0), fi=ease(t,0,.9);
  ambient(c,t,.55*fi,'rgb(150,200,255)'); c.save(); c.globalAlpha*=fi; c.fillStyle='rgba(3,5,8,.55)'; c.fillRect(0,0,W,H); c.restore();
  // a frequency rule across the top, scanning
  c.save(); c.globalAlpha*=fi*.8; for(let i=0;i<=48;i++){ const x=60+i*(W-120)/48; c.fillStyle='rgba(255,255,255,.28)'; c.fillRect(x,92,i%6===0?2:1,i%6===0?22:10); } const mx=lerp(160,W-160,eio3(seg(t,.3,Q0-.1)));
  c.fillStyle=RED; c.fillRect(mx-1,70,3,56); txt(c,'TUNING',60,60,{size:20,w:800,col:'#8d97a1',ls:6}); c.restore();
  kicker(c,'SINCE THE LAST VIDEO',W/2,330,ease(t,.6,.6)*(1-seg(t,Q0-1.7,Q0-1.5)),RED,'center');
  if(t<Q0-1.6){ let i=0; while(i<RV.length-2&&t>=RV[i+1][0]) i++; const v=RV[i][1], since=t-RV[i][0], prev=i>0?RV[i-1][1]:null, k=eio3(clamp(since/.2)), yy=640, step=290;
    c.save(); c.beginPath(); c.rect(0,yy-240,W,300); c.clip(); c.globalAlpha*=ease(t,.3,.8)*(i===2?1-seg(t,Q0-1.8,Q0-1.6):1);
    if(prev&&k<1) txt(c,prev,W/2,yy-k*step,{size:250,w:700,al:'center',col:'#5d6670',ls:-10}); txt(c,v,W/2,yy+(1-k)*step,{size:250,w:700,al:'center',col:i?'#aeb8c2':'#7d8691',ls:-10}); c.restore();
    if(RVN[v]) slam(c,RVN[v],W/2,760,t,RV[i][0]+.1,{size:58,w:700,col:'#d7dee6',t1:(RV[i+1]?RV[i+1][0]-.05:Q0-1.7)}); }
  else { const on=BE(t), p=lock, sc=lerp(.85,1,eio3(seg(t,Q0-1.7,Q0-1.3))); const [sx,sy]=[(hash(t*91)-.5)*34*p*p,(hash(t*57)-.5)*20*p*p];
    c.save(); c.translate(W/2+sx,640+sy); c.scale(sc,sc); c.globalAlpha*=.4+.6*on; c.shadowColor='rgba(255,90,70,.95)'; c.shadowBlur=80*on; txt(c,V,0,0,{size:lerp(250,400,p),w:900,al:'center',col:on>.45?'#fff':RED,ls:-14}); c.restore();
    kicker(c,t<Q0-.35?'LOCKING':'LOCKED',W/2,330,1,t<Q0-.35?'#ffb3a8':'#7be3a1','center'); }
  // the lock: four brackets close on the number
  if(lock>0){ const e=eio3(lock), bw=lerp(1500,640,e), bh=lerp(700,300,e), cx=W/2, cy=560, L=60; c.save(); c.strokeStyle=t<Q0?'#ff8c7d':'#7be3a1'; c.lineWidth=5; c.globalAlpha*=.4+.6*lock;
    for(const [sx,sy] of [[-1,-1],[1,-1],[1,1],[-1,1]]){ const x=cx+sx*bw/2, y=cy+sy*bh/2; c.beginPath(); c.moveTo(x-sx*L,y); c.lineTo(x,y); c.lineTo(x,y-sy*L); c.stroke(); } c.restore(); }
  hud(c,t,fi); flash(c,BE(t)*.1*seg(t,2,Q0));
}
// ---------------------------------------------------------------- 1. the band comes in: name and logo, the numbers, the count
const A1=nearEv(ALLEV,Q0+4.9,.4,.6), A2=nearEv(ALLEV,Q0+10.5,.4,.6);
function sTitle(c,t){
  const lt=t-Q0, [sx,sy]=shake(t,26); c.fillStyle='#04060a'; c.fillRect(0,0,W,H); ambient(c,t,.5+.4*SE(t),'rgb(120,170,255)'); c.save(); c.fillStyle='rgba(3,5,8,.45)'; c.fillRect(0,0,W,H); c.restore(); c.save(); c.translate(sx,sy);
  const s=lerp(1.8,1,eio3(clamp(lt/.3)))*(1+.02*KE(t)); c.save(); c.translate(W/2,330); c.scale(s,s); c.shadowColor='rgba(255,90,70,.7)'; c.shadowBlur=110*Math.exp(-lt*2)+34; c.drawImage(logo7,-185,-185,370,370); c.restore();
  slam(c,[['OnAir ',0],[V,1]],W/2,740,t,Q0+.04,{size:200,from:1.7});
  const words=['TV.','Radio.','Aircraft.','Ships.','Satellites.','Pagers.','Sensors.','Time signals.','And more.']; const sn=evIn(SNARE,Q0+1.0,A1-.3,.25), n=Math.min(words.length,sn.length); let wi=-1; for(let k=0;k<n;k++) if(t>=sn[k]) wi=k;
  if(wi>=0){ const w=wi>=n-1&&n<words.length?'And more.':words[Math.min(wi,words.length-1)]; slam(c,w,W/2,880,t,sn[wi],{size:84,w:700,col:'#d7dee6',from:1.3,d:.1}); }
  c.restore(); hud(c,t); flash(c,hitNow(t,[Q0])*.95+SE(t)*.1);
}
const STAT=[['652','commits since 0.2.4',652],['16','people wrote them',16],['44','new modes',44]];
function sStats(c,t){
  const [sx,sy]=shake(t,22); c.fillStyle='#04060a'; c.fillRect(0,0,W,H); ambient(c,t,.45+.3*KE(t),'rgb(255,150,120)'); c.save(); c.fillStyle='rgba(3,5,8,.5)'; c.fillRect(0,0,W,H); c.restore(); c.save(); c.translate(sx,sy);
  const d=(A2-A1)/3; STAT.forEach(([big,small,n],k)=>{ const t0=nearEv(ALLEV,A1+k*d,.4,.3), t1=k<2?nearEv(ALLEV,A1+(k+1)*d,.4,.3)-.05:A2-.05; if(t<t0-.05||t>t1+.25) return; const cnt=Math.round(n*eio3(seg(t,t0,t0+Math.min(.9,(t1-t0)*.5))));
    kicker(c,k===0?'THE LAST FEW WEEKS':k===1?'WRITTEN BY':'NEW',W/2,300,ease(t,t0,.4)*(1-seg(t,t1,t1+.2)),RED,'center');
    slam(c,String(cnt),W/2,650,t,t0,{size:big.length>2?470:520,w:900,t1,glow:true,from:1.6}); slam(c,small,W/2,780,t,t0+.35,{size:70,w:700,t1,col:'#d7dee6'}); ring(c,W/2,540,t,t0); });
  c.restore(); hud(c,t); flash(c,SE(t)*.18);
}
function sCount(c,t){
  const lt=t-A2, [sx,sy]=shake(t,24); c.fillStyle='#04060a'; c.fillRect(0,0,W,H); ambient(c,t,.9,'rgb(150,200,255)'); c.save(); c.fillStyle='rgba(3,5,8,.4)'; c.fillRect(0,0,W,H); c.restore(); c.save(); c.translate(sx,sy);
  const p=eio3(seg(t,A2,A2+2.6)), n=Math.round(120*p), big=n>=120;
  c.save(); c.translate(W/2,560); const s_=big?1+.16*KE(t):1; c.scale(s_,s_); if(big){ c.shadowColor='rgba(255,90,70,.85)'; c.shadowBlur=100; } txt(c,String(n),0,0,{size:600,w:900,al:'center',col:big?'#fff':'#d4dbe2',ls:-26}); c.restore();
  slam(c,'modes',W/2,690,t,A2+.1,{size:84,w:700,col:big?RED:'#9aa6b2'});
  // the fourteen groups, one chip on each kick after the count
  const ks=evIn(KICK,A2+2.7,Q1-.2,.3), per=Math.max(1,Math.floor(ks.length/14)); const ch=GROUPS.map((g,i)=>({g,i,t0:ks[Math.min(ks.length-1,i*per)]||A2+3+i*.3}));
  c.font='700 26px Inter'; const ws=ch.map(o=>c.measureText(o.g.name).width+40), rows=[ch.slice(0,5),ch.slice(5,10),ch.slice(10)];
  rows.forEach((row,r)=>{ const tot=row.reduce((a,o)=>a+ws[o.i]+14,-14); let x=W/2-tot/2; row.forEach(o=>{ const k=eob(seg(t,o.t0,o.t0+.2)); if(k>0){ c.save(); c.globalAlpha*=clamp(k*2); c.translate(x+ws[o.i]/2,790+r*62); c.scale(k,k); rr(c,-ws[o.i]/2,-25,ws[o.i],50,25); c.fillStyle=rgba(o.g.col,.22); c.fill(); c.strokeStyle=o.g.col; c.lineWidth=2.5; c.stroke(); txt(c,o.g.name,0,9,{size:25,w:800,al:'center'}); c.restore(); } x+=ws[o.i]+14; }); });
  kicker(c,'IN FOURTEEN GROUPS',W/2,738,ease(t,A2+2.6,.4),RED,'center',22);
  c.restore(); hud(c,t); flash(c,SE(t)*.15+(big?KE(t)*.1:0)); }
// ---------------------------------------------------------------- 2. the fourteen groups on a wheel: it snaps to the next group on the song's own hits
const GREP=['o_dvb','o_dab','o_adsb','o_ais','n_hrpt','o_dmr','o_cell','o_pager','o_wifi','o_sonde','o_ft8','o_m17','o_ale','o_specan'];
const GT=(()=>{ const T=[]; let last=Q1-.3; for(let i=0;i<14;i++){ const want=Q1+i*(Q2-Q1-.2)/14; let t=nearEv(ALLEV,want,.35,.18); if(t<=last+.25) t=Math.max(want,last+.25); T.push(t); last=t; } return T; })();
const GTEX=GROUPS.map(()=>mk(900,560));
function groupTex(g,t,lt){ const G=GROUPS[g], x=GTEX[g].getContext('2d'); x.setTransform(1,0,0,1,0,0); x.fillStyle='#0a0e13'; x.fillRect(0,0,900,560);
  const gg=x.createLinearGradient(0,0,900,560); gg.addColorStop(0,rgba(G.col,.42)); gg.addColorStop(1,'rgba(0,0,0,0)'); x.fillStyle=gg; x.fillRect(0,0,900,560);
  const im=img(c7(GREP[g],loopT(GREP[g],t,.6))); if(im){ x.save(); x.beginPath(); x.rect(470,28,402,232); x.clip(); const cm=crop7(GREP[g]); x.translate(670,144); x.scale(402/(W/cm.z),402/(W/cm.z)); x.translate(-cm.cx,-cm.cy); x.drawImage(im,0,0,W,H); x.restore(); x.strokeStyle=G.col; x.lineWidth=3; x.strokeRect(470,28,402,232); }
  txt(x,String(g+1).padStart(2,'0')+' / 14',34,64,{size:26,w:800,mono:true,col:G.col}); const nm=G.name, parts=nm.length>13&&nm.includes(' ')?[nm.slice(0,nm.indexOf(' ',Math.ceil(nm.length/2)-4)),nm.slice(nm.indexOf(' ',Math.ceil(nm.length/2)-4)+1)]:[nm];
  parts.forEach((l,j)=>txt(x,l,34,140+j*62,{size:56,w:800})); const nw=G.members.filter(m=>m.new).length; txt(x,G.members.length+' modes'+(nw?' · '+nw+' new':''),34,140+parts.length*62+16,{size:30,w:700,col:'#c8d2dc'});
  const cols=G.members.length>9?3:2, per=Math.ceil(G.members.length/cols), cw=cols===3?280:420; G.members.forEach((m,j)=>{ const k=lt==null?1:seg(lt,.12+j*.03,.24+j*.03); if(k<=0) return; const cx=34+Math.floor(j/per)*cw, cy=330+(j%per)*(per>7?27:34); x.globalAlpha=k; txt(x,m.n,cx,cy,{size:per>7?21:25,w:m.new?800:500,col:m.new?'#fff':'#8f9aa6'}); if(m.new){ x.fillStyle=RED; x.beginPath(); x.arc(cx-12,cy-7,4,0,7); x.fill(); } x.globalAlpha=1; });
  x.strokeStyle=rgba(G.col,.9); x.lineWidth=4; x.strokeRect(2,2,896,556); return GTEX[g]; }
function sWheel(c,t){
  const D14=Math.PI*2/14, R=2050; let i=0; while(i<13&&t>=GT[i+1]) i++; const ph=t<GT[0]?-1:(i-1+eob(seg(t,GT[i],GT[i]+.3))); const phi=ph*D14, lt=t-GT[i];
  c.fillStyle='#04060a'; c.fillRect(0,0,W,H); const G=GROUPS[i]; const gg=c.createRadialGradient(W/2,H*.45,0,W/2,H*.45,W*.7); gg.addColorStop(0,rgba(G.col,.32)); gg.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=gg; c.fillRect(0,0,W,H);
  ambient(c,t,.28,null); const [sx,sy]=shake(t,16); c.save(); c.translate(sx,sy);
  const zoom=1+.06*KE(t); c.translate(W/2,H*.46); c.scale(zoom,zoom); c.translate(-W/2,-H*.46);
  const items=[]; for(let g=0;g<14;g++){ let th=g*D14-phi; th=((th+Math.PI)%(Math.PI*2)+Math.PI*2)%(Math.PI*2)-Math.PI; if(Math.abs(th)>1.75) continue; items.push({g,th}); } items.sort((a,b)=>Math.abs(b.th)-Math.abs(a.th));
  items.forEach(({g,th})=>{ const act=g===i, cs=screen3(c,groupTex(g,t,act?lt:null),{pos:[R*Math.sin(th),-40,R*(1-Math.cos(th))-120-(act?70*KE(t):0)],w:act?1020:900,h:act?637:562,ry:-th,rx:0,a:act?1:clamp(1-Math.abs(th)*.45),glow:act?1:0,nx:14,ny:9}); });
  c.restore(); for(let j=0;j<14;j++){ const f=j<i?1:j===i?seg(t,GT[i],GT[i+1]||Q2):0; c.fillStyle='rgba(255,255,255,.12)'; c.fillRect(110+j*128,H-150,118,6); c.fillStyle=GROUPS[j].col; c.fillRect(110+j*128,H-150,118*f,6); }
  slam(c,'Fourteen groups.',110,150,t,Q1+.05,{size:88,al:'left',t1:Q1+2,from:1.3}); hud(c,t); flash(c,hitNow(t,[GT[i]])*.2+hitNow(t,[Q1])*.5);
}
// ---------------------------------------------------------------- 3. the 44 new modes, nine to a page, each tile landing on a real kick
const PAGES=5, PT=Array.from({length:PAGES+1},(_,p)=>p===0?Q2:p===PAGES?Q3:nearEv(SNARE,Q2+p*(Q3-Q2)/PAGES,.5,.5));
const PLAY=[[-.2,.07],[.22,.05],[-.1,-.08],[.12,.1],[0,0]];
function sNew(c,t){
  let p=0; while(p<PAGES-1&&t>=PT[p+1]) p++; const a=PT[p], b=PT[p+1], lt=t-a, out=seg(t,b-.2,b), items=NEWM.slice(p*9,p*9+9); const [rx_,ry_]=[PLAY[p][1],PLAY[p][0]];
  c.fillStyle='#04060a'; c.fillRect(0,0,W,H); ambient(c,t,.3+.2*KE(t),'rgb(130,170,255)'); c.save(); c.fillStyle='rgba(3,5,8,.5)'; c.fillRect(0,0,W,H); c.restore();
  const ks=evIn(KICK,a+.05,b-.5,.28), used=[]; const [sx,sy]=shake(t,14); c.save(); c.translate(sx,sy); const zo=1+out*.4; c.translate(W/2,545); c.scale(zo,zo); c.translate(-W/2,-545); c.globalAlpha*=1-.85*out;
  const tw=450, th=253, gx=30, gy=26; items.forEach((m,k)=>{ const want=a+.15+k*(b-a-.9)/items.length; let tk=ks.reduce((best,x)=>used.includes(x)?best:(best==null||Math.abs(x-want)<Math.abs(best-want)?x:best),null); if(tk==null) tk=want; used.push(tk); if(t<tk) return;
    const col=k%3, row=Math.floor(k/3), q=rot3([(col-1)*(tw+gx),(row-1)*(th+gy)+5,0],rx_,ry_), age=t-tk, sc=eob(seg(age,0,.22)), cl=clipOf(m);
    const cs=screen3(c,cl?tex7(TEXS[k],cl,loopT(cl,t*.9+k*.4)):null,{pos:[q[0],q[1],q[2]+(1-sc)*-400],w:tw*sc,h:th*sc,rx:rx_,ry:ry_,a:clamp(age/.08),glow:.45,nx:8,ny:5});
    c.save(); c.globalAlpha*=clamp(age/.1); c.beginPath(); cs.forEach((pp,j)=>j?c.lineTo(pp[0],pp[1]):c.moveTo(pp[0],pp[1])); c.closePath(); c.strokeStyle=m.col; c.lineWidth=3; c.stroke();
    const s=cs[3][2]; txt(c,m.n,cs[3][0]+12*s,cs[3][1]-14*s,{size:Math.max(12,34*s),w:800,col:'#fff'}); c.restore();
    if(age<.16){ c.save(); c.globalCompositeOperation='lighter'; c.globalAlpha*=(1-age/.16)*.7; c.beginPath(); cs.forEach((pp,j)=>j?c.lineTo(pp[0],pp[1]):c.moveTo(pp[0],pp[1])); c.closePath(); c.fillStyle='#fff'; c.fill(); c.restore(); } });
  c.restore(); const shown=p*9+items.filter((m,k)=>used[k]!=null&&t>=used[k]).length;
  kicker(c,'NEW IN 0.2.7',110,120,1,RED); txt(c,String(Math.min(44,shown)).padStart(2,'0')+' / 44',W-110,128,{size:64,w:900,mono:true,al:'right',col:'#fff'});
  hud(c,t); flash(c,hitNow(t,[a])*.5+KE(t)*.1);
}
// ---------------------------------------------------------------- 4. the features: a live screen in 3D that pushes in on the part that matters, a big statement beside it
const FEAT=window.FEAT7||[];
const FPL=(()=>{ const bins=[[0,3,Q3,Q4],[3,11,Q4,Q5],[11,14,Q5,Q6],[14,16,Q6,Q6+8.1]], out=[]; for(const [i0,i1,a,b] of bins){ const n=i1-i0; let last=a; for(let k=0;k<n;k++){ const t0=k===0?a:nearEv(ALLEV,a+k*(b-a)/n,.45,.4); out.push([i0+k,t0,k===n-1?b:nearEv(ALLEV,a+(k+1)*(b-a)/n,.45,.4)]); } } return out; })();
const ZOOM={f_touchmodes:{cx:640,cy:330,z:1.75},f_txdry:{cx:1450,cy:520,z:1.9},f_images:{cx:1210,cy:720,z:2.3},f_passive:{cx:470,cy:430,z:2.0},f_df:{cx:300,cy:340,z:2.5},f_packs:{cx:800,cy:420,z:2.0},f_rfi:{cx:500,cy:300,z:2.2},f_passes:{cx:430,cy:330,z:2.3},f_map:{cx:400,cy:500,z:2.2},f_hc:{cx:1050,cy:420,z:1.9},f_second:{cx:1112,cy:640,z:1.6},f_p25:{cx:1112,cy:620,z:1.7}};
function featScene(c,t,i,t0,t1){
  const f=FEAT[i]; if(!f){ cine(c,t); return; } const lt=t-t0, dur=t1-t0, p=seg(t,t0,t1), left=i%2===0, inK=eio3(seg(lt,-.03,.28)), outK=eio3(seg(t,t1-.22,t1)); const [sx,sy]=shake(t,12);
  c.fillStyle='#04060a'; c.fillRect(0,0,W,H); const gg=c.createRadialGradient(left?W*.7:W*.3,H*.45,0,left?W*.7:W*.3,H*.45,W*.6); gg.addColorStop(0,rgba(f.col||'rgb(92,168,219)',.34)); gg.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=gg; c.fillRect(0,0,W,H); ambient(c,t,.14,null);
  c.save(); c.translate(sx,sy); const clip=f.clip&&has7(f.clip)?f.clip:null, side=left?1:-1;
  if(clip){ const c0=f.cm||crop7(clip), c1=ZOOM[clip]||c0, k=eio3(seg(lt,.55,Math.max(.9,dur-.5))), cm={cx:lerp(c0.cx,c1.cx,k),cy:lerp(c0.cy,c1.cy,k),z:lerp(c0.z,c1.z,k)};
    screen3(c,tex7(TEXL[2],clip,(f.t0||3)+(f.rate===0?0:lt*(f.rate||.8)),cm),{pos:[side*(lerp(1500,430,inK)+outK*1200),10,lerp(900,100,inK)+lerp(0,-40,p)],w:1360,ry:-side*(lerp(.95,.3,inK)-.05*p),rx:.03,glow:.75,sheen:p,a:1-outK*.6,nx:20,ny:12}); }
  else if(f.draw) f.draw(c,t,lt,p,left,inK);
  c.save(); const gs=c.createLinearGradient(left?0:W,0,left?W*.6:W*.4,0); gs.addColorStop(0,'rgba(3,5,8,.9)'); gs.addColorStop(1,'rgba(3,5,8,0)'); c.fillStyle=gs; c.fillRect(0,0,W,H); c.restore();
  const x=left?110:W-110, al=left?'left':'right'; kicker(c,f.kick||'NEW IN 0.2.7',x,270,ease(lt,0,.3)*(1-outK),RED,al);
  f.head.forEach((h,j)=>slam(c,[[h[0],h[1]]],x,410+j*128,t,t0+.04+j*.12,{size:f.head.length>1&&h[0].length>13?108:120,al,from:1.3,d:.14,t1:t1-.2}));
  const yb=410+f.head.length*128+26; c.save(); c.globalAlpha*=(1-outK); (f.bullets||[]).forEach((b,j)=>{ const k=seg(t,t0+.45+j*.25,t0+.65+j*.25); if(k<=0) return; c.save(); c.globalAlpha*=k; c.translate((1-k)*(left?-20:20),0); c.fillStyle=RED; c.beginPath(); c.arc(left?x+6:x-6,yb+j*58-11,7,0,7); c.fill(); txt(c,b,left?x+28:x-28,yb+j*58,{size:34,w:500,col:'#e1e8ef',al}); c.restore(); }); c.restore();
  c.restore(); hud(c,t); flash(c,hitNow(t,[t0])*.4+SE(t)*.08); }
function sFeat(c,t){ let k=FPL.length-1; for(let j=0;j<FPL.length;j++) if(t<FPL[j][2]){ k=j; break; } const [i,t0,t1]=FPL[k]; featScene(c,t,i,t0,t1); }
// ---------------------------------------------------------------- 5. the stream of names, into the full stop of the section; the wall; the montage; the finale
function nameRows(c,t,speed,a){ for(let r=0;r<8;r++){ const y=130+r*104, dir=r%2?1:-1; c.save(); c.globalAlpha*=a*(.35+.65*hash(r*3.7)); c.font='800 64px Inter'; let x=((t*speed*dir*(1+r*.07))%3600+3600)%3600-3600;
    for(let k=0;k<MODES.length*2&&x<W+200;k++){ const m=MODES[(r*13+k)%MODES.length]; txt(c,m.n,x,y,{size:64,w:800,col:k%5===0?m.col:'#2a323b'}); x+=c.measureText(m.n).width+70; } c.restore(); } }
const G0=Q6, G1=nearEv(ALLEV,Q6+4,.45,.4), G2=nearEv(ALLEV,Q6+8,.45,.4);
function sRun(c,t){ c.fillStyle='#030405'; c.fillRect(0,0,W,H); const p=seg(t,G2,Q7), sp=lerp(300,2600,p*p); nameRows(c,t,sp,1); const [sx,sy]=shake(t,lerp(8,28,p)); c.save(); c.translate(sx,sy);
  c.save(); c.fillStyle='rgba(3,5,8,.62)'; c.fillRect(0,H/2-160,W,250); c.restore(); slam(c,'A hundred and twenty modes.',W/2,H/2+34,t,G2+.05,{size:118,t1:Q7-1.3}); slam(c,[['All of them',0],[' in one app.',1]],W/2,H/2+34,t,Q7-1.25,{size:134,t1:Q7-.04}); c.restore(); hud(c,t); flash(c,BE(t)*.16*p); }
function sWall(c,t){
  c.fillStyle='#04060a'; c.fillRect(0,0,W,H); const lt=t-Q7, span=Q8-Q7, k=eio3(seg(lt,0,span)), [sx,sy]=shake(t,26); const gl=c.createRadialGradient(W/2,H*.45,0,W/2,H*.45,W*.7); gl.addColorStop(0,'rgba(90,140,200,.3)'); gl.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=gl; c.fillRect(0,0,W,H); c.save(); c.translate(sx,sy);
  const cols=12, tw=300, th=169, gx=22, gy=22, rows=10, ry=lerp(-.5,.1,k), rx=lerp(.45,.08,k), off=[lerp(-700,0,k),lerp(300,-10,k),lerp(2800,1500,eio3(seg(lt,0,1.4)))+lerp(0,-300,k)];
  const flick=ALLEV.filter(e=>e[0]<=t&&t-e[0]<.18).map(e=>Math.floor(hash(e[0]*91)*MODES.length));
  const items=MODES.map((m,i)=>{ const gxp=((i%cols)-(cols-1)/2)*(tw+gx), gyp=(Math.floor(i/cols)-(rows-1)/2)*(th+gy), q=rot3([gxp,gyp,0],rx,ry); return {m,i,gxp,gyp,pos:[q[0]+off[0],q[1]+off[1],q[2]+off[2]]}; }).sort((a,b)=>b.pos[2]-a.pos[2]);
  items.forEach(it=>{ const fly=clamp(lt*3-hash(it.i*1.7)*1.4); if(fly<=0) return; const cl=clipOf(it.m), P=(u,v)=>{ const q=rot3([it.gxp+(u-.5)*tw,it.gyp+(v-.5)*th,0],rx,ry); return proj([q[0]+off[0],q[1]+off[1],q[2]+off[2]]); }; const cs=[P(0,0),P(1,0),P(1,1),P(0,1)]; if(cs.some(p=>p[2]<=0)) return;
    c.save(); c.globalAlpha*=clamp(fly); if(cl) screen3(c,img('file:///tmp/clips7/'+cl+'/thumb.jpg'),{pos:it.pos,w:tw,h:th,rx,ry,nx:2,ny:2,glow:it.m.new?.5:0}); else { c.beginPath(); cs.forEach((p,j)=>j?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.closePath(); c.fillStyle='#0d1219'; c.fill(); }
    c.beginPath(); cs.forEach((p,j)=>j?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.closePath(); if(flick.includes(it.i)){ c.fillStyle='rgba(255,255,255,.55)'; c.fill(); } c.strokeStyle=it.m.new?RED:rgba(it.m.col,.8); c.lineWidth=it.m.new?3:1.6; c.stroke();
    const s=cs[0][2]; txt(c,it.m.n,cs[3][0]+8*s,cs[3][1]-10*s,{size:Math.max(8,22*s),w:800,col:'#fff'}); c.restore(); });
  c.save(); c.fillStyle='rgba(3,5,8,.5)'; c.fillRect(0,H/2-150,W,250); c.restore(); slam(c,[['ONE ',0],['APP.',1]],W/2,H/2+50,t,Q7,{size:240,from:2.3,glow:true,t1:Q8-1}); c.restore(); hud(c,t); flash(c,hitNow(t,[Q7])*1.0+KE(t)*.06,'255,235,225'); ring(c,W/2,H/2,t,Q7,'255,200,180',1400);
}
const MKS=evIn(KICK,Q8,Q9,.3), MON7=MODES.filter((m,i)=>clipOf(m)).sort((a,b)=>hash(a.n.length*7.3+a.n.charCodeAt(0))-hash(b.n.length*7.3+b.n.charCodeAt(0)));
function sMontage(c,t){ const ki=Math.max(0,lastIdx(MKS,t)), tk=MKS[ki]||Q8, m=MON7[ki%MON7.length], cl=clipOf(m), ltb=t-tk; const [sx,sy]=shake(t,16);
  c.fillStyle='#000'; c.fillRect(0,0,W,H); c.save(); c.translate(sx,sy); if(cl){ const cm=crop7(cl); foot(c,c7(cl,loopT(cl,t*.8+ki*.3)),{...cm,z:cm.z*(1.05+.1*seg(ltb,0,.5))}); } c.fillStyle='rgba(3,5,8,.34)'; c.fillRect(0,0,W,H); scrim(c,1);
  slam(c,m.n,110,H-190,t,tk,{size:120,al:'left',from:1.25,d:.09}); txt(c,GROUPS[m.g].name,112,H-290,{size:28,w:800,col:m.col,ls:5}); const cnt=Math.round(120*(ki+1)/MKS.length);
  txt(c,String(Math.min(120,cnt)).padStart(3,'0')+' / 120',W-110,H-160,{size:76,w:900,mono:true,al:'right',col:'#fff'}); c.restore(); hud(c,t,.9); flash(c,hitNow(t,[tk])*.28+SE(t)*.1); }
function lastIdx(arr,t){ let lo=0,hi=arr.length-1,r=-1; while(lo<=hi){ const m=(lo+hi)>>1; if(arr[m]<=t){ r=m; lo=m+1; } else hi=m-1; } return r; }
function sFinale(c,t){
  const [sx,sy]=shake(t,18); c.fillStyle='#04060a'; c.fillRect(0,0,W,H); ambient(c,t,.5+.3*SE(t),'rgb(150,200,255)'); c.save(); c.fillStyle='rgba(3,5,8,.52)'; c.fillRect(0,0,W,H); c.restore(); c.save(); c.translate(sx,sy);
  slam(c,[['Free.',0],[' Open source.',1]],W/2,470,t,Q9+.03,{size:160,t1:END-.9}); slam(c,'macOS · Windows · Linux · Android',W/2,620,t,nearEv(ALLEV,Q9+1.6,.4,.4),{size:62,w:600,col:'#c8d2dc',t1:END-.9});
  const bd=seg(t,END-2.2,END); if(bd>0){ c.save(); c.fillStyle=`rgba(0,0,0,${.75*bd})`; c.fillRect(0,0,W,H); c.restore(); }
  c.restore(); hud(c,t,1-seg(t,END-.5,END)); flash(c,hitNow(t,[Q9])*.5+SE(t)*.1); }
function sEnd(c,t){ const lt=t-END, [sx,sy]=shake(t,30); c.fillStyle='#04060a'; c.fillRect(0,0,W,H); ambient(c,t,.45+.3*SE(t),'rgb(255,160,130)'); c.save(); c.fillStyle='rgba(3,5,8,.5)'; c.fillRect(0,0,W,H); c.restore(); c.save(); c.translate(sx,sy);
  const s=lerp(1,1.04,seg(t,END,LEN)); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2); c.save(); c.shadowColor='rgba(255,90,70,.7)'; c.shadowBlur=100*Math.exp(-lt*1.2)+24; c.drawImage(logo7,W/2-200,130,400,400); c.restore();
  slam(c,[['OnAir ',0],[V,1]],W/2,700,t,END,{size:160,from:1.8}); slam(c,'github.com/adswill/OnAir',W/2,810,t,END+.25,{size:64,w:700,col:'#8cc8ff',from:1.3});
  txt(c,'Many of the new modes are experimental and tested on generated signals: reports and recordings welcome.',W/2,890,{size:25,w:400,al:'center',col:'#8d97a1',a:seg(t,END+.7,END+1.3)}); c.restore(); hud(c,t,seg(t,END+1,END+2)*.9); flash(c,hitNow(t,[END])*1.0,'255,235,225'); ring(c,W/2,330,t,END,'255,180,160',1500); }
// ---------------------------------------------------------------- timeline, frame loop, post (chromatic aberration on the snare, a punch on the kick, vignette, grain)
const SEQ=[[0,Q0,sIntro],[Q0,A1,sTitle],[A1,A2,sStats],[A2,Q1,sCount],[Q1,Q2,sWheel],[Q2,Q3,sNew],[Q3,Q6,sFeat],[Q6,G1,(c,t)=>featScene(c,t,14,G0,G1)],[G1,G2,(c,t)=>featScene(c,t,15,G1,G2)],[G2,Q7,sRun],[Q7,Q8,sWall],[Q8,Q9,sMontage],[Q9,END,sFinale],[END,LEN+1,sEnd]];
window.TOTAL=LEN; window.NF=Math.round(LEN*FPS7);
const TA=mk(), tax=TA.getContext('2d'), TB=mk(), tbx=TB.getContext('2d'), TRB=[Q1,Q2,Q3,Q4,Q5,Q6,Q7,Q8,Q9], TRD=.24;   // chapter boundaries that get a punch-through transition
function frame7(t){ songWF(t); let k=SEQ.findIndex(q=>t>=q[0]&&t<q[1]); if(k<0) k=SEQ.length-1; const s=SEQ[k], tb=TRB.find(b=>t>=b&&t<b+TRD);
  if(tb!=null&&k>0&&SEQ[k][0]===tb){ const e=eio3((t-tb)/TRD), prev=SEQ[k-1]; tbx.save(); tbx.clearRect(0,0,W,H); s[2](tbx,t); tbx.restore(); tax.save(); tax.clearRect(0,0,W,H); prev[2](tax,t); tax.restore();
    ctx.fillStyle='#000'; ctx.fillRect(0,0,W,H); ctx.save(); ctx.translate(W/2,H/2); const sb=lerp(.86,1,e); ctx.scale(sb,sb); ctx.translate(-W/2,-H/2); ctx.drawImage(TB,0,0); ctx.restore();
    ctx.save(); ctx.translate(W/2,H/2); const sa=lerp(1,1.55,e); ctx.scale(sa,sa); ctx.translate(-W/2,-H/2); ctx.globalAlpha=1-e; ctx.filter=`blur(${(e*14).toFixed(1)}px)`; ctx.drawImage(TA,0,0); ctx.restore(); return; }
  ctx.save(); s[2](ctx,t); ctx.restore(); }
const PB=mk(), pbx=PB.getContext('2d');
function post7(t,frame){
  const big=hitNow(t,[Q0,Q1,Q2,Q3,Q4,Q5,Q6,Q7,Q8,Q9,END]), sn=SE(t), kk=KE(t), k=Math.max(big*1.0,sn*.55,kk*.25);
  if(k>.05||kk>.08){ const z=1+.016*kk+.03*big; pbx.clearRect(0,0,W,H); pbx.drawImage(cv,0,0); ctx.save(); ctx.fillStyle='#000'; ctx.fillRect(0,0,W,H); ctx.translate(W/2,H/2); ctx.scale(z,z); ctx.translate(-W/2,-H/2);
    if(k>.05){ ctx.globalCompositeOperation='lighter'; const o=1.5+13*k; ctx.filter='url(#fr)'; ctx.drawImage(PB,-o,0); ctx.filter='url(#fg)'; ctx.drawImage(PB,0,0); ctx.filter='url(#fb)'; ctx.drawImage(PB,o,0); } else ctx.drawImage(PB,0,0); ctx.restore(); }
  const g=ctx.createRadialGradient(W/2,H/2,H*.55,W/2,H/2,H*1.05); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.34)'); ctx.fillStyle=g; ctx.fillRect(0,0,W,H);
  ctx.save(); ctx.globalAlpha=.03; ctx.globalCompositeOperation='overlay'; const ox=(hash(frame*1.7)*256)|0, oy=(hash(frame*2.3)*256)|0; for(let x=-ox;x<W;x+=256) for(let y=-oy;y<H;y+=256) ctx.drawImage(grain,x,y); ctx.restore();
  const o=seg(t,LEN-1.6,LEN); if(o>0){ ctx.fillStyle=`rgba(0,0,0,${o})`; ctx.fillRect(0,0,W,H); } const f=1-seg(t,0,.6); if(f>0){ ctx.fillStyle=`rgba(0,0,0,${f})`; ctx.fillRect(0,0,W,H); } }
window.ready=Promise.all([window.ready,logo7.decode()]);
window.renderFrame=async(i)=>{ const t=i/FPS7; NEED=new Set(); frame7(t); const need=NEED; NEED=null; if(need.size){ await ensure(need); frame7(t); } post7(t,i); return t; };
