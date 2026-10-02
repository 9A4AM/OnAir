const {chromium}=require('/opt/node-tools/node_modules/playwright');
const path=require('path');
(async()=>{
  const args=process.argv.slice(2); const mode=args[0];
  const b=await chromium.launch({args:['--allow-file-access-from-files','--disable-web-security']});
  const url='file://'+path.resolve(__dirname,'index.html');
  async function mkpage(){const p=await b.newPage({viewport:{width:1920,height:1080}}); p.on('pageerror',e=>console.log('ERR',e.message)); await p.goto(url); await p.evaluate(()=>window.ready); return p;}
  if(mode==='preview'){ const p=await mkpage(); console.log(await p.evaluate(()=>JSON.stringify({T:window.TOTAL,S:window.SCENES,N:window.NF})));
    for(const f of args.slice(1)){ await p.evaluate(i=>window.renderFrame(i),+f); await p.screenshot({path:`frames/p_${String(f).padStart(5,'0')}.jpg`,type:'jpeg',quality:85}); } }
  else { const nw=+args[1]||4; const pages=await Promise.all(Array.from({length:nw},mkpage)); const N=await pages[0].evaluate(()=>window.NF); let next=0;
    await Promise.all(pages.map(async p=>{ while(true){const i=next++; if(i>=N)break; await p.evaluate(i=>window.renderFrame(i),i); await p.screenshot({path:`frames/f_${String(i).padStart(5,'0')}.jpg`,type:'jpeg',quality:93}); if(i%100===0)console.log(i,'/',N);} })); }
  await b.close();
})();
