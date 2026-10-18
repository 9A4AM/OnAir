// OnAir 0.2.7: what changed since 0.2.4, cut to a rock song (the song file is never in the repository; see footage/README.md).
// Every cut, slam and hit sits on the song's measured beat grid (beatmap027.js): BM.beats, BM.down (downbeats), BM.band (the band comes in),
// BM.stop / BM.slam (the full stop and the slam back), BM.join (the splice to the ending), BM.final (the last hit).
// Modes, groups and the one-line descriptions are the app's own mode table (groups027.js, generated from app/ui/mode_table.cpp on the 0.2.7 branch).
// Footage: the unmodified 0.2.7 build, each mode on its built-in test signal (clips7: n_<id> new modes, o_<id> older ones, f_<name> features).
const RED='#ff5a46', HOT='#ffd2c8', V='0.2.7';
const DBT=BM.down, BEATS=BM.beats, BAND=BM.band, STOP=BM.stop, SLAM=BM.slam, JOIN=BM.join, FIN=BM.final, LEN=BM.len||FIN+5.6;
const D=i=>DBT[Math.min(i,DBT.length-1)];
// the plan: which downbeat each chapter starts on (beatmap027.js may carry BM.plan for another song); defaults are the Thunderstruck edit
const PL=Object.assign({stats:[[1,0],[3,1],[5,2],[7,3],[9,4],[11,5],[13,6]],statDur:2,stats2:null,title:20,band:16,count:20,groups:22,newStart:36,newSlow:18,newFast:2,feat:56,featList:Array(16).fill(2),run:88,fin1:103,fin2:104},BM.plan||{});
const FSTART=[]; { let b=PL.feat; for(const n of PL.featList){ FSTART.push(b); b+=n; } FSTART.push(b); }
const logo7=new Image(); logo7.src='assets/logo_027.png';
const c7=(n,t)=>'file:///tmp/clips7/'+n+'/f_'+pad(clamp(Math.floor(t*30),0,(window.C7N[n]||240)-1)+1,5)+'.jpg';
const CROPD={cx:1112,cy:575,z:1.19};
const crop7=n=>(window.CROPS7&&window.CROPS7[n])||CROPD;
const has7=n=>!!(window.C7N&&window.C7N[n]);
function prevIn(arr,t){ let lo=0,hi=arr.length-1,r=-1; while(lo<=hi){ const m=(lo+hi)>>1; if(arr[m]<=t){ r=m; lo=m+1; } else hi=m-1; } return r; }
const beatK=t=>{ const i=prevIn(BEATS,t); return i<0?0:Math.exp(-(t-BEATS[i])*9); };
const downK=t=>{ const i=prevIn(DBT,t); return i<0?0:Math.exp(-(t-DBT[i])*5); };
const hitK=(t,h,r=4)=>t<h?0:Math.exp(-(t-h)*r);
const barOf=t=>prevIn(DBT,t);
function tex7(buf,n,t,cm){ return tex(buf,x=>foot(x,c7(n,t),cm||crop7(n))); }
function shakeOf(t,amt){ const k=downK(t)*amt; return [(hash(Math.floor(t*60)*1.3)-.5)*k,(hash(Math.floor(t*60)*2.1)-.5)*k]; }
function flash(c,k,col='255,255,255'){ if(k<=.01) return; c.save(); c.globalCompositeOperation='lighter'; c.fillStyle=`rgba(${col},${Math.min(1,k)})`; c.fillRect(0,0,W,H); c.restore(); }
function kicker(c,s,x,y,a=1,col=RED,al='left',size=26){ if(a<=0) return; txt(c,s,x,y,{size,w:800,col,ls:7,a,al}); }
function rgba(col,a){ return col.replace('rgb','rgba').replace(')',','+a+')'); }
function ring(c,x,y,t,t0,col='255,120,100',R=900){ const k=seg(t,t0,t0+.7); if(k<=0||k>=1) return; c.save(); c.globalCompositeOperation='lighter'; c.strokeStyle=`rgba(${col},${.6*(1-k)})`; c.lineWidth=2+10*(1-k); c.beginPath(); c.arc(x,y,eio3(k)*R,0,7); c.stroke(); c.restore(); }
// the modes, flat: [{n,id,sub,g,new,clip,col}]
const MODES=GROUPS.flatMap((g,gi)=>g.members.map(m=>({...m,g:gi,col:g.col,clip:(m.new?'n_':'o_')+m.id})));
const NEWM=MODES.filter(m=>m.new);
const clipOf=m=>has7(m.clip)?m.clip:null;
// a dim, flickering layer of real screens behind type: one mode per beat
function bgFoot(c,t,a,list=NEWM,step=1){ const bi=prevIn(BEATS,t); const m=list[((bi*step)%list.length+list.length)%list.length], cl=clipOf(m); if(!cl) return;
  c.save(); c.globalAlpha*=a; foot(c,c7(cl,5+(t-BEATS[bi])),{...crop7(cl),z:crop7(cl).z*1.1}); c.restore(); }

// ---------------------------------------------------------------- 1. cold open: the riff alone; the numbers since 0.2.4, one per two bars
const STATS0=[['0.2.5','86 changes','RELEASED'],['0.2.6','202 changes','RELEASED'],['0.2.7','364 changes, and counting','NOW'],['652','commits since 0.2.4',''],['16','people wrote them',''],['44','new modes',''],['120','modes in all','']];
const statSlam=(c,t,bar,k,dur)=>{ const [big,small,k_]=STATS0[k], t0=D(bar), t1=D(bar+dur)-.2; if(t<t0-.1||t>t1+.3) return; if(k_) kicker(c,k_,W/2,330,ease(t,t0,.4)*(1-seg(t,t1,t1+.2)),RED,'center');
  slam(c,big,W/2,640,t,t0,{size:big.length>4?330:420,w:900,t1,glow:true,from:1.9}); slam(c,small,W/2,760,t,t0+.5,{size:62,w:700,t1,col:'#d7dee6'}); ring(c,W/2,560,t,t0); };
function sIntro(c,t){
  c.fillStyle='#030405'; c.fillRect(0,0,W,H);
  const sk=seg(t,.3,BAND-2);
  c.save(); c.globalAlpha=.5*ease(t,0,.8); waterfall(c,0,0,W,H,t+3,[{u:.5,w:.34,k:sk*sk}],.05); c.fillStyle='rgba(4,6,9,.74)'; c.fillRect(0,0,W,H); c.restore();
  if(t>=D(1)) bgFoot(c,t,.22*seg(t,D(0),D(0)+1)*(1-seg(t,D(PL.band-1),D(PL.band))));
  c.save(); c.globalAlpha=ease(t,.2,1); spectrum(c,0,780,W,300,t+3,[{u:.5,w:.34,k:sk*sk}],{n:600,lw:2.4,noFrame:true,grid:false,band:false}); c.restore();
  kicker(c,'SINCE 0.2.4',W/2,300,ease(t,D(0),.5)*(1-seg(t,D(PL.band-1)-.2,D(PL.band-1))),RED,'center');
  PL.stats.forEach(([bar,k])=>statSlam(c,t,bar,k,PL.statDur));
  if(t>=D(PL.band-1)){ const p=seg(t,D(PL.band-1),BAND), on=beatK(t); const [sx,sy]=[(hash(t*91)-.5)*30*p,(hash(t*57)-.5)*20*p];
    c.save(); c.translate(sx,sy); c.globalAlpha=.35+.65*on; c.shadowColor='rgba(255,90,70,.9)'; c.shadowBlur=70*on; txt(c,V,W/2,650,{size:lerp(240,400,p),w:900,al:'center',col:on>.5?'#fff':RED,ls:-14}); c.restore(); flash(c,.3*on*p); }
}
// ---------------------------------------------------------------- 2. the band comes in: the name and the new logo, then a dolly through live screens
function sTitle(c,t){
  const lt=t-BAND, [sx,sy]=shakeOf(t,20); cine(c,t,{glow:.3,gy:H*.45}); c.save(); c.translate(sx,sy);
  const k=eio3(seg(lt,0,6)), n=NEWM.filter(m=>clipOf(m)).length;
  for(let j=0;j<7;j++){ const m=NEWM[(j*5+3)%NEWM.length], cl=clipOf(m); if(!cl) continue; const q=((j+lt*.12)%7)/7;
    blurred(c,lerp(10,2,Math.abs(q-.5)*2),x=>screen3(x,tex7(TEXS[j],cl,5+lt*.5),{pos:[lerp(-1500,1500,q),lerp(-380,340,hash(j*3.3)),lerp(1500,500,Math.abs(q-.5)*2)],w:520,h:292,ry:lerp(.5,-.5,q),rx:.1,a:.55*Math.sin(Math.PI*q),glow:.3,nx:6,ny:4})); }
  const s=lerp(1.7,1,eio3(clamp(lt/.32)))*(1+.012*beatK(t)); c.save(); c.translate(W/2,330); c.scale(s,s); c.shadowColor='rgba(255,90,70,.6)'; c.shadowBlur=100*hitK(t,BAND,2)+30; c.drawImage(logo7,-185,-185,370,370); c.restore();
  slam(c,[['OnAir ',0],[V,1]],W/2,730,t,BAND+.05,{size:190,from:1.6});
  const wAll=['TV.','Radio.','Aircraft.','Ships.','Satellites.','Pagers.','Sensors.','Time signals.','And more.'], avail=D(PL.title)-BAND-1.3, step=Math.max(.45,Math.min(.9,avail/wAll.length)), nw=Math.max(3,Math.min(wAll.length,Math.floor(avail/step))); const words=nw>=wAll.length?wAll:wAll.slice(0,nw-1).concat(['And more.']); const wi=clamp(Math.floor((lt-1.3)/step),-1,words.length-1);
  if(wi>=0){ const bl=BEATS.filter(b=>b>=BAND+1.3).slice(0,words.length*2); const st=bl[Math.min(wi*2,bl.length-1)]||BAND+1.3; slam(c,words[wi],W/2,890,t,BAND+1.3+wi*step,{size:78,w:700,col:'#d7dee6',from:1.3,d:.12}); }
  c.restore(); flash(c,hitK(t,BAND,5)*.9);
}
// ---------------------------------------------------------------- 3. the count
function sStats(c,t){ cine(c,t,{glow:.25}); const [sx,sy]=shakeOf(t,16); c.save(); c.translate(sx,sy); bgFoot(c,t,.2,NEWM,3); PL.stats2.forEach(([bar,k])=>statSlam(c,t,bar,k,1)); c.restore(); flash(c,hitK(t,D(PL.stats2[0][0]),6)*.3); }
function sCount(c,t){
  cine(c,t,{glow:.2}); const [sx,sy]=shakeOf(t,14); c.save(); c.translate(sx,sy);
  const p=eio3(seg(t,D(PL.count),D(PL.count+1))), n=Math.round(120*p), big=t>=D(PL.count+1);
  bgFoot(c,t,.16,MODES,2);
  c.save(); c.translate(W/2,690); const s_=big?1+.22*hitK(t,D(PL.count+1),5):1+.04*beatK(t); c.scale(s_,s_); if(big){ c.shadowColor='rgba(255,90,70,.85)'; c.shadowBlur=100; }
  txt(c,String(n),0,0,{size:620,w:900,al:'center',col:big?'#fff':'#d4dbe2',ls:-28}); c.restore();
  slam(c,'modes',W/2,860,t,D(PL.count)+.1,{size:80,w:700,col:big?RED:'#9aa6b2',t1:null}); slam(c,'in fourteen groups.',W/2,960,t,D(PL.count+1)+.05,{size:56,w:600,col:'#d7dee6'});
  c.restore(); flash(c,hitK(t,D(PL.count+1),6)*.55,'255,120,100'); ring(c,W/2,560,t,D(PL.count+1));
}
// ---------------------------------------------------------------- 4. the fourteen groups, one bar each
const GREP=['o_dvb','o_dab','o_adsb','o_ais','n_hrpt','o_dmr','o_cell','o_pager','o_wifi','o_sonde','o_ft8','o_m17','o_ale','o_specan'];
function sGroups(c,t){
  const g=clamp(barOf(t)-PL.groups,0,13), G=GROUPS[g], t0=D(PL.groups+g), lt=t-t0, bar=D(PL.groups+g+1)-t0, p=seg(lt,0,bar);
  cine(c,t,{glow:.18}); c.save(); const gg=c.createRadialGradient(W*.7,H*.5,0,W*.7,H*.5,W*.7); gg.addColorStop(0,rgba(G.col,.3)); gg.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=gg; c.fillRect(0,0,W,H); c.restore();
  const [sx,sy]=shakeOf(t,12); c.save(); c.translate(sx,sy);
  const inK=eio3(seg(lt,-.04,.2));
  screen3(c,tex7(TEXL[0],GREP[g],5+lt*.7),{pos:[lerp(1500,500,inK),-10,lerp(900,110,inK)+lerp(0,-30,p)],w:1260,ry:lerp(-1,-.32,inK)+.05*p,rx:.03,glow:.6,sheen:p,nx:18,ny:10});
  const nw=G.members.filter(m=>m.new).length;
  c.save(); c.fillStyle=G.col; c.fillRect(110,200,lerp(0,150,inK),6); c.restore();
  kicker(c,String(g+1).padStart(2,'0')+' / 14',110,170,ease(lt,0,.2),G.col);
  const nm=G.name, parts=nm.length>13&&nm.includes(' ')?[nm.slice(0,nm.indexOf(' ',Math.ceil(nm.length/2)-4)),nm.slice(nm.indexOf(' ',Math.ceil(nm.length/2)-4)+1)]:[nm];
  parts.forEach((l,j)=>slam(c,l,110,320+j*108,t,t0+.02+j*.05,{size:parts.length>1?104:128,al:'left',from:1.3,d:.14}));
  const y0=320+parts.length*108+10; txt(c,G.members.length+' modes'+(nw?' · '+nw+' new':''),112,y0,{size:34,w:700,col:'#d7dee6',a:seg(lt,.12,.3)});
  const cols=G.members.length>12?3:2, per=Math.ceil(G.members.length/cols), cw=cols===3?300:370;
  G.members.forEach((m,j)=>{ const ta=t0+.2+j*(bar*.62/G.members.length), k=seg(t,ta,ta+.1); if(k<=0) return; const cx=110+Math.floor(j/per)*cw, cy=y0+54+(j%per)*(cols===3?46:50);
    c.save(); c.globalAlpha*=k; c.translate((1-k)*-18,0); txt(c,m.n,cx,cy,{size:cols===3?27:31,w:m.new?800:500,col:m.new?'#fff':'#7f8a96'}); if(m.new){ c.fillStyle=RED; c.beginPath(); c.arc(cx-16,cy-9,5,0,7); c.fill(); } c.restore(); });
  // the progress rail
  for(let j=0;j<14;j++){ const f=j<g?1:j===g?p:0; c.fillStyle='rgba(255,255,255,.1)'; c.fillRect(110+j*128,H-60,118,6); c.fillStyle=GROUPS[j].col; c.fillRect(110+j*128,H-60,118*f,6); }
  c.restore(); flash(c,hitK(t,t0,9)*.22);
}
// ---------------------------------------------------------------- 5. the 44 new modes, as fast as the song goes
const NSLOTS=(()=>{ const s=[]; let i=PL.newStart; for(let k=0;k<PL.newSlow;k++,i++){ const a=D(i), b=D(i+1), m=(a+b)/2; s.push([a,m],[m,b]); } for(let k=0;k<PL.newFast;k++,i++){ const a=D(i), b=D(i+1), q=(b-a)/4; for(let j=0;j<4;j++) s.push([a+j*q,a+(j+1)*q]); } return s; })();
function sNew(c,t){
  let si=NSLOTS.findIndex(s=>t>=s[0]&&t<s[1]); if(si<0) si=NSLOTS.length-1; const [a,b]=NSLOTS[si], lt=t-a, p=seg(t,a,b); const m=NEWM[Math.min(si,NEWM.length-1)], cl=clipOf(m), fast=si>=PL.newSlow*2;
  const [sx,sy]=shakeOf(t,fast?22:10);
  cine(c,t,{glow:.12}); c.save(); c.translate(sx,sy);
  if(cl){ if(si%2===0||fast){ foot(c,c7(cl,4+lt*.8),{...crop7(cl),z:crop7(cl).z*lerp(1.0,1.12,p)}); c.fillStyle='rgba(3,5,8,.28)'; c.fillRect(0,0,W,H); }
    else { c.save(); const gg=c.createRadialGradient(W*.5,H*.5,0,W*.5,H*.5,W*.6); gg.addColorStop(0,rgba(m.col,.28)); gg.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=gg; c.fillRect(0,0,W,H); c.restore();
      screen3(c,tex7(TEXL[1],cl,4+lt*.8),{pos:[lerp(si%4===1?260:-260,si%4===1?-120:120,p),-30,lerp(160,40,p)],w:1460,ry:(si%4===1?-1:1)*lerp(.34,.16,p),rx:.06,glow:.6,sheen:p,nx:16,ny:9}); } }
  scrim(c,1); const nmSize=fast?150:m.n.length>16?108:132;
  slam(c,m.n,100,H-168,t,a+.01,{size:nmSize,al:'left',from:1.25,d:.1,col:'#fff'});
  if(!fast) txt(c,m.sub,104,H-96,{size:36,w:500,col:'#c8d2dc',a:seg(lt,.08,.2)});
  c.fillStyle=m.col; c.fillRect(100,H-66,lerp(0,300,eio3(seg(lt,0,.2))),6); txt(c,GROUPS[m.g].name,100,H-36,{size:24,w:700,col:m.col,ls:3});
  kicker(c,'NEW',W-130-170,H-62,1,RED,'right',24); txt(c,String(si+1).padStart(2,'0')+' / 44',W-100,H-60,{size:34,w:800,mono:true,al:'right',col:'#fff'});
  txt(c,'MODES NEW IN 0.2.7',100,120,{size:24,w:800,col:RED,ls:7,a:.9});
  c.restore(); flash(c,hitK(t,a,fast?7:10)*(fast?.34:.28));
}
// ---------------------------------------------------------------- 6. what else changed: two bars each, a live screen in 3D beside a statement
const FEAT=window.FEAT7||[];
function sFeat(c,t){
  const n=FEAT.length||1, i=Math.max(0,Math.min(n-1,prevIn(FSTART,barOf(t)))), t0=D(FSTART[i]), t1=D(FSTART[i+1]), lt=t-t0, p=seg(t,t0,t1), f=FEAT[i]; if(!f){ cine(c,t); return; }
  const left=i%2===0, [sx,sy]=shakeOf(t,10); cine(c,t,{glow:.18,gy:H*.5}); c.save();
  const gg=c.createRadialGradient(left?W*.7:W*.3,H*.5,0,left?W*.7:W*.3,H*.5,W*.55); gg.addColorStop(0,rgba(f.col||'rgb(92,168,219)',.28)); gg.addColorStop(1,'rgba(0,0,0,0)'); c.fillStyle=gg; c.fillRect(0,0,W,H); c.restore();
  c.save(); c.translate(sx,sy); const inK=eio3(seg(lt,-.05,.26));
  const clip=f.clip&&has7(f.clip)?f.clip:null;
  if(clip) screen3(c,tex7(TEXL[2],clip,(f.t0||3)+lt*(f.rate||.8),f.cm),{pos:[(left?1:-1)*lerp(1500,430,inK),20,lerp(900,100,inK)+lerp(0,-26,p)],w:1300,ry:(left?-1:1)*(lerp(.95,.3,inK)-.05*p),rx:.03,glow:.6,sheen:p,nx:20,ny:12});
  else if(f.draw) f.draw(c,t,lt,p,left,inK);
  c.save(); const gs=c.createLinearGradient(left?0:W,0,left?W*.62:W*.38,0); gs.addColorStop(0,'rgba(3,5,8,.86)'); gs.addColorStop(1,'rgba(3,5,8,0)'); c.fillStyle=gs; c.fillRect(0,0,W,H); c.restore();
  const x=left?110:W-110, al=left?'left':'right';
  kicker(c,f.kick||'NEW IN 0.2.7',x,300,ease(lt,0,.3),RED,al);
  f.head.forEach((h,j)=>slam(c,[[h[0],h[1]]],x,430+j*124,t,t0+.05+j*.16,{size:f.head.length>1&&h[0].length>13?100:112,al,from:1.3,d:.16,t1:t1-.18}));
  const yb=430+f.head.length*124+14; c.save(); c.globalAlpha*=(1-seg(t,t1-.18,t1)); (f.bullets||[]).forEach((b,j)=>{ const k=seg(t,t0+.5+j*.3,t0+.7+j*.3); if(k<=0) return; c.save(); c.globalAlpha*=k; c.translate((1-k)*(left?-20:20),0);
    c.fillStyle=RED; c.beginPath(); c.arc(left?x+6:x-6,yb+j*52-10,6,0,7); c.fill(); txt(c,b,left?x+26:x-26,yb+j*52,{size:31,w:500,col:'#d7dee6',al}); c.restore(); }); c.restore();
  c.restore(); flash(c,hitK(t,t0,8)*.25);
}
// ---------------------------------------------------------------- 7. the full stop, and the slam: all 120 on a wall
function nameRows(c,t,speed,a){ for(let r=0;r<9;r++){ const y=150+r*105, dir=r%2?1:-1; c.save(); c.globalAlpha*=a*(.35+.65*hash(r*3.7)); c.font='800 64px Inter'; let x=((t*speed*dir*(1+r*.07))%3600+3600)%3600-3600;
    for(let k=0;x<W+200;k++){ const m=MODES[(r*13+k)%MODES.length]; txt(c,m.n,x,y,{size:64,w:800,col:k%5===0?m.col:'#2a323b'}); x+=c.measureText(m.n).width+70; } c.restore(); } }
function sRun(c,t){ // the last two bars before the stop: every name streams past
  c.fillStyle='#030405'; c.fillRect(0,0,W,H); const p=seg(t,D(PL.run),STOP), sp=lerp(300,2600,p*p);
  nameRows(c,t,sp,1); const [sx,sy]=shakeOf(t,lerp(8,28,p)); c.save(); c.translate(sx,sy);
  c.save(); c.fillStyle='rgba(3,5,8,.58)'; c.fillRect(0,H/2-150,W,260); c.restore();
  slam(c,'A hundred and twenty modes.',W/2,H/2+40,t,D(PL.run)+.05,{size:116,t1:STOP-.9}); slam(c,[['All of them',0],[' in one app.',1]],W/2,H/2+40,t,STOP-.85,{size:132,t1:STOP-.06});
  c.restore(); flash(c,beatK(t)*.18*p);
}
function sStop(c,t){ c.fillStyle='#000'; c.fillRect(0,0,W,H); txt(c,'Ready?',W/2,H/2+20,{size:40,w:500,al:'center',col:'#8d97a1',a:seg(t,STOP+.15,STOP+.5)*(1-seg(t,SLAM-.08,SLAM)),ls:4}); }
function sWall(c,t){
  cine(c,t,{glow:.3}); const lt=t-SLAM, span=JOIN-SLAM, k=eio3(seg(lt,0,span)), [sx,sy]=shakeOf(t,24); c.save(); c.translate(sx,sy);
  const cols=12, tw=300, th=169, gx=22, gy=22, rows=10;
  const ry=lerp(-.5,.12,k), rx=lerp(.45,.1,k), off=[lerp(-700,0,k),lerp(300,0,k),lerp(2800,1500,eio3(seg(lt,0,1.4)))+lerp(0,-300,k)];
  const items=MODES.map((m,i)=>{ const gxp=((i%cols)-(cols-1)/2)*(tw+gx), gyp=(Math.floor(i/cols)-(rows-1)/2)*(th+gy); const q=rot3([gxp,gyp,0],rx,ry); return {m,i,gxp,gyp,pos:[q[0]+off[0],q[1]+off[1],q[2]+off[2]]}; }).sort((a,b)=>b.pos[2]-a.pos[2]);
  items.forEach(it=>{ const fly=clamp(lt*3-hash(it.i*1.7)*1.4); if(fly<=0) return; const cl=clipOf(it.m);
    const P=(u,v)=>{ const q=rot3([it.gxp+(u-.5)*tw,it.gyp+(v-.5)*th,0],rx,ry); return proj([q[0]+off[0],q[1]+off[1],q[2]+off[2]]); };
    const cs=[P(0,0),P(1,0),P(1,1),P(0,1)]; if(cs.some(p=>p[2]<=0)) return; c.save(); c.globalAlpha*=clamp(fly);
    if(cl){ screen3(c,img(c7(cl,5)),{pos:it.pos,w:tw,h:th,rx,ry,nx:2,ny:2,a:1,glow:it.m.new?.5:0}); }
    else { c.beginPath(); cs.forEach((p,j)=>j?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.closePath(); c.fillStyle='#0d1219'; c.fill(); }
    c.beginPath(); cs.forEach((p,j)=>j?c.lineTo(p[0],p[1]):c.moveTo(p[0],p[1])); c.closePath(); c.strokeStyle=it.m.new?RED:rgba(it.m.col,.8); c.lineWidth=it.m.new?3:1.6; c.stroke();
    const s=cs[0][2]; txt(c,it.m.n,cs[3][0]+8*s,cs[3][1]-10*s,{size:Math.max(8,22*s),w:800,col:'#fff'}); c.restore(); });
  c.save(); c.fillStyle='rgba(3,5,8,.5)'; c.fillRect(0,H/2-140,W,250); c.restore();
  slam(c,[['ONE ',0],['APP.',1]],W/2,H/2+60,t,SLAM,{size:240,from:2.3,glow:true,t1:SLAM+span-1});
  c.restore(); flash(c,hitK(t,SLAM,3)*1.0,'255,235,225'); ring(c,W/2,H/2,t,SLAM,'255,200,180',1400);
}
// ---------------------------------------------------------------- 8. after the splice: a new mode on every beat, then the last hit
const MONT7=MODES.filter(m=>clipOf(m)&&m.new).concat(MODES.filter(m=>clipOf(m)&&!m.new));
function sFinale(c,t){
  if(t<D(PL.fin1)){ const bi=prevIn(BEATS,t), b0=prevIn(BEATS,JOIN), m=MONT7[((bi-b0)*3+MONT7.length*4)%MONT7.length], cl=clipOf(m), ltb=t-BEATS[bi];
    const [sx,sy]=shakeOf(t,12); c.fillStyle='#000'; c.fillRect(0,0,W,H); c.save(); c.translate(sx,sy);
    if(cl) foot(c,c7(cl,5+ltb),{...crop7(cl),z:crop7(cl).z*(1.04+.08*seg(ltb,0,.44))}); c.fillStyle='rgba(3,5,8,.38)'; c.fillRect(0,0,W,H); scrim(c,1);
    slam(c,m.n,110,H-110,t,BEATS[bi],{size:104,al:'left',from:1.2,d:.1}); txt(c,GROUPS[m.g].name,112,H-212,{size:26,w:800,col:m.col,ls:5});
    c.restore(); flash(c,beatK(t)*.2); return; }
  if(t<FIN){ cine(c,t,{glow:.25}); const [sx,sy]=shakeOf(t,12); c.save(); c.translate(sx,sy);
    slam(c,[['Free.',0],[' Open source.',1]],W/2,470,t,D(PL.fin1)+.05,{size:150}); slam(c,'macOS · Windows · Linux · Android',W/2,610,t,D(PL.fin2),{size:60,w:600,col:'#c8d2dc'});
    const bd=seg(t,FIN-1.8,FIN); if(bd>0){ c.save(); c.fillStyle=`rgba(0,0,0,${.7*bd})`; c.fillRect(0,0,W,H); c.restore(); } c.restore(); flash(c,hitK(t,D(PL.fin1)+.05,7)*.3); return; }
  cine(c,t,{glow:.32,gy:H*.4}); const k=hitK(t,FIN,1.2), s=lerp(1,1.05,seg(t,FIN,LEN)); c.save(); c.translate(W/2,H/2); c.scale(s,s); c.translate(-W/2,-H/2);
  c.save(); c.shadowColor='rgba(255,90,70,.6)'; c.shadowBlur=90*k+20; c.drawImage(logo7,W/2-200,150,400,400); c.restore();
  slam(c,[['OnAir ',0],[V,1]],W/2,720,t,FIN,{size:150,from:1.8}); slam(c,'github.com/adswill/OnAir',W/2,820,t,FIN+.25,{size:62,w:700,col:'#8cc8ff',from:1.3});
  txt(c,'Many of the new modes are experimental and tested on generated signals: reports and recordings welcome.',W/2,900,{size:25,w:400,al:'center',col:'#8d97a1',a:seg(t,FIN+.6,FIN+1.2)});
  c.restore(); flash(c,hitK(t,FIN,5)*.8,'255,235,225'); ring(c,W/2,350,t,FIN,'255,180,160',1500);
}
// ---------------------------------------------------------------- timeline, frame loop, post (chromatic aberration on the beat, vignette, grain)
const SEQ=[[0,BAND,sIntro],[BAND,D(PL.stats2?PL.title:PL.count),sTitle],...(PL.stats2?[[D(PL.title),D(PL.count),sStats]]:[]),[D(PL.count),D(PL.groups),sCount],[D(PL.groups),D(PL.newStart),sGroups],[D(PL.newStart),D(PL.feat),sNew],[D(PL.feat),D(PL.run),sFeat],[D(PL.run),STOP,sRun],[STOP,SLAM,sStop],[SLAM,JOIN,sWall],[JOIN,LEN+1,sFinale]];
window.TOTAL=LEN; window.NF=Math.round(LEN*FPS);
function frame7(t){ const s=SEQ.find(q=>t>=q[0]&&t<q[1])||SEQ[SEQ.length-1]; ctx.save(); s[2](ctx,t); ctx.restore(); }
const PB=mk(), pbx=PB.getContext('2d');
function post7(t,frame){
  const hits=[BAND,D(PL.count+1),D(PL.feat),SLAM,FIN]; let k=0; for(const h of hits) k=Math.max(k,hitK(t,h,6)); k=Math.max(k,.35*beatK(t)*(t>D(PL.newStart)?1:.4));
  if(k>.04){ pbx.clearRect(0,0,W,H); pbx.drawImage(cv,0,0); ctx.save(); ctx.fillStyle='#000'; ctx.fillRect(0,0,W,H); ctx.globalCompositeOperation='lighter'; const o=2+14*k;
    ctx.filter='url(#fr)'; ctx.drawImage(PB,-o,0); ctx.filter='url(#fg)'; ctx.drawImage(PB,0,0); ctx.filter='url(#fb)'; ctx.drawImage(PB,o,0); ctx.restore(); }
  const g=ctx.createRadialGradient(W/2,H/2,H*.55,W/2,H/2,H*1.05); g.addColorStop(0,'rgba(0,0,0,0)'); g.addColorStop(1,'rgba(0,0,0,.32)'); ctx.fillStyle=g; ctx.fillRect(0,0,W,H);
  ctx.save(); ctx.globalAlpha=.03; ctx.globalCompositeOperation='overlay'; const ox=(hash(frame*1.7)*256)|0, oy=(hash(frame*2.3)*256)|0; for(let x=-ox;x<W;x+=256) for(let y=-oy;y<H;y+=256) ctx.drawImage(grain,x,y); ctx.restore();
  const o=seg(t,LEN-1.6,LEN); if(o>0){ ctx.fillStyle=`rgba(0,0,0,${o})`; ctx.fillRect(0,0,W,H); }
}
window.ready=Promise.all([window.ready,logo7.decode()]);
window.renderFrame=async(i)=>{ const t=i/FPS; NEED=new Set(); frame7(t); const need=NEED; NEED=null; if(need.size){ await ensure(need); frame7(t); } post7(t,i); return t; };
