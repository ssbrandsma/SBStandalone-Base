const fs=require('fs'),assert=require('assert'),{JSDOM}=require('jsdom');
(async()=>{
 for(const protocol of ['http','https']){
  const dom=new JSDOM('<header></header><nav id="tabs"></nav><main id="app"></main>',{url:protocol+'://192.168.1.78/',runScripts:'outside-only',pretendToBeVisual:true}),w=dom.window,calls=[];
  w.fetch=async(url,options={})=>{calls.push({url,options});return {ok:!options.body,json:async()=>url==='/api/secure-portal'?{available:true,port:443}:{authenticated:false,error:'fixture'}}};
  w.eval(fs.readFileSync(process.argv[2]+'/js/secure.js','utf8'));
  w.eval(fs.readFileSync(process.argv[2]+'/js/app.js','utf8'));await new Promise(r=>setTimeout(r,20));
  assert.strictEqual(w.securePortalUrl({available:true,port:8443},'vpn'),'https://192.168.1.78:8443/#vpn');assert.strictEqual(w.securePortalUrl({available:true,port:0}),'');
  const input=w.document.querySelector('#pair-pin'),form=w.document.querySelector('#pair-form');assert(input);
  if(protocol==='https'){
   assert(input.disabled);assert(w.document.body.textContent.includes('SHA-256'));assert(w.document.body.textContent.includes('Secure Web Pairing Code'));
   await form.onsubmit({preventDefault(){}});assert(!calls.some(x=>x.options.body),'No PIN before physical certificate confirmation');
   const confirmed=w.document.querySelector('#certificate-confirmed');confirmed.checked=true;confirmed.onchange();assert(!input.disabled);input.value='123456';
   await form.onsubmit({preventDefault(){}});assert(calls.some(x=>x.url==='/api/auth/pair'&&x.options.body));
  }else{
   assert(!input.disabled);assert(w.document.querySelector('a[href="https://192.168.1.78/"]'));assert(!w.document.querySelector('input[type=password]'));
  }
  w.close();
 }
 console.log('Secure portal DOM: HTTPS URL/port, HTTP link, certificate confirmation before PIN entry and no HTTP secret editor passed');
})().catch(e=>{console.error(e);process.exitCode=1});
