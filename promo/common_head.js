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

