const {chromium}=require('/opt/node-tools/node_modules/playwright'); const path=require('path');
(async()=>{ const b=await chromium.launch({args:['--allow-file-access-from-files']}); const p=await b.newPage({viewport:{width:1920,height:1080}}); p.on('pageerror',e=>console.log('ERR',e.message));
 await p.goto('file://'+path.resolve(__dirname,'thumb.html')); await p.evaluate(()=>window.drawThumb()); await p.screenshot({path:'thumbnail.png'}); await b.close(); })();
