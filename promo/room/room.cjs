const {chromium}=require('/opt/node-tools/node_modules/playwright'); const path=require('path'); const fs=require('fs');
(async()=>{ const b=await chromium.launch({args:['--allow-file-access-from-files','--disable-web-security','--use-angle=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist']});
 const mode=process.argv[2]; const p=await b.newPage({viewport:{width:1920,height:1080}}); p.on('pageerror',e=>console.log('ERR',e.message)); p.on('console',m=>{ if(m.type()==='error') console.log('CONSOLE',m.text()); });
 await p.goto('file://'+path.resolve(__dirname,'room.html')); await p.waitForFunction(()=>window.renderFrame); await p.evaluate(()=>window.ready);
 const out=process.env.OUT||'/tmp/roomf'; fs.mkdirSync(out,{recursive:true});
 let frames; if(mode==='preview') frames=process.argv.slice(3).map(Number); else { const [a,bb]=process.argv.slice(3).map(Number); frames=[]; for(let i=a;i<=bb;i++) frames.push(i); }
 for(const f of frames){ const t0=Date.now(); await p.evaluate(i=>window.renderFrame(i),f); const buf=await p.screenshot({type:'jpeg',quality:92}); fs.writeFileSync(`${out}/r_${String(f).padStart(4,'0')}.jpg`,buf); if(mode==='preview') console.log(f,Date.now()-t0,'ms'); }
 await b.close(); })();
