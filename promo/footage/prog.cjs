const {chromium}=require('/opt/node-tools/node_modules/playwright'); const path=require('path'); const fs=require('fs');
(async()=>{ const b=await chromium.launch(); const p=await b.newPage({viewport:{width:1280,height:720}}); await p.goto('file://'+path.resolve(__dirname,'prog.html')); await p.evaluate(()=>window.ready);
 fs.mkdirSync('/tmp/progf',{recursive:true}); const N=+process.argv[2]||500; for(let i=0;i<N;i++){ await p.evaluate(i=>window.frame(i),i); await p.screenshot({path:`/tmp/progf/p_${String(i).padStart(4,'0')}.jpg`,type:'jpeg',quality:90}); } await b.close(); })();
