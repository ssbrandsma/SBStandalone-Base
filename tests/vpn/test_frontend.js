// Execute the real page against a DOM, using synthetic data and no network.
const fs=require('fs'),assert=require('assert');
let JSDOM;try{({JSDOM}=require('jsdom'))}catch(e){console.log('SKIP: node-jsdom development dependency required');process.exit(77)}
const source=fs.readFileSync(process.argv[2],'utf8');
async function fixture(url){
 const dom=new JSDOM('<main id="app"></main>',{url,runScripts:'outside-only',pretendToBeVisual:true}),w=dom.window;
 const id='a'.repeat(32),writes=[],scrolls=[],intervals=[];
 const status={state:'CONNECTED',available:true,enabled:true,selected_profile_id:id,active_profile_id:id,configured_country:'Netherlands',connection_duration_seconds:10,public_ip:'8.8.4.4',public_ip_verified:true,public_ip_checked_at:100,verified_country:'US',verified_country_name:'United States',verified_region:'New York',verified_city:'New York',verification:{state:'verified',age_seconds:2,refresh_after_seconds:0,location_state:'verified'},traffic:{available:true,collecting:true,completed:false,session_id:1,profile_id:id,rx_bytes:1500000,tx_bytes:2500,rx_bytes_per_second:16000,tx_bytes_per_second:1000,source:'sysfs',sample_age_seconds:1}};
 const profile={id,name:'Fixture profile',country:'Netherlands',country_code:'NL',provider:'Fixture',server:'192.0.2.1',authentication:'password',credentials_configured:false};
 w.app=w.document.querySelector('#app');w.esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));w.facts=()=>'';
 w.setInterval=(fn,delay)=>{intervals.push({fn,delay});return intervals.length};w.clearInterval=()=>{};w.HTMLElement.prototype.scrollIntoView=function(){scrolls.push(this.id)};
 w.load=async path=>path==='/api/secure-portal'?{available:true,port:443}:path.endsWith('/capabilities')?{available:true,config_limit:49152,kill_switch_available:false}:path.endsWith('/profiles')?[profile]:path.endsWith('/settings')?{selected:id,reconnect:true,dns:true}:path.endsWith('/logs')?[]:JSON.parse(JSON.stringify(status));
 w.write=async(path,options)=>{writes.push({path,method:options.method,body:JSON.parse(options.body)});return {}};
 w.securePortalUrl=(info,page)=>'https://'+w.location.hostname+'/#'+page;w.eval(source);await w.vpnPage();return {w,id,writes,scrolls,status,intervals};
}
(async()=>{
 const {w,id,writes,scrolls,status,intervals}=await fixture('http://127.0.0.1:8088/');
 assert.strictEqual(w.vpnFormatBytes(999),'999 B');assert.strictEqual(w.vpnFormatBytes(1000),'1.0 KB');assert.strictEqual(w.vpnFormatBytes(1500000),'1.5 MB');assert.strictEqual(w.vpnFormatBytes(1000000000),'1.0 GB');assert.strictEqual(w.vpnFormatBytes(2500,true),'2.5 KB/s');assert.strictEqual(w.vpnFormatBytes(null),'Unavailable');
 assert.strictEqual(w.document.querySelector('#vpn-public-ip').textContent,'8.8.4.4');assert(w.document.querySelector('#vpn-location-country').textContent.includes('United States'));assert.strictEqual(w.document.querySelector('#vpn-location-detail').textContent,'New York / New York');assert.strictEqual(w.document.querySelector('#vpn-rx-total').textContent,'1.5 MB');
 const button=w.document.querySelector('[data-vpn-edit]');assert(!button.disabled);button.click();
 let f=w.document.querySelector('#vpn-edit');assert(f,'Editor did not open');assert(scrolls.includes('vpn-profile-form'),'Editor was not revealed');assert.strictEqual(w.document.activeElement,f.elements.name,'Editor did not receive focus');
 const originalForm=f;f.elements.username.value='unfinished draft';const selected=w.document.querySelector('#vpn-profile').value;status.traffic.rx_bytes=2000000;
 const poll=intervals.find(x=>x.delay===2000);assert(poll,'Two-second backend polling is missing');await intervals.filter(x=>x.delay===2000).at(-1).fn();
 assert.strictEqual(w.document.querySelector('#vpn-edit'),originalForm);assert.strictEqual(f.elements.username.value,'unfinished draft');assert.strictEqual(w.document.activeElement,f.elements.name);assert.strictEqual(w.document.querySelector('#vpn-profile').value,selected);assert.strictEqual(w.document.querySelector('#vpn-rx-total').textContent,'2.0 MB');assert.strictEqual(writes.length,0,'Status polling must not trigger external verification');
 f.elements.username.value='fixture-service-user';f.elements.password_operation.value='replace';f.elements.password.value='synthetic-not-a-real-password';
 await f.onsubmit({target:f,preventDefault(){}});
 assert.strictEqual(writes.length,1);assert.strictEqual(writes[0].path,'/api/vpn/profiles/'+id);assert.strictEqual(writes[0].method,'PUT');assert.strictEqual(writes[0].body.username,'fixture-service-user');assert.strictEqual(writes[0].body.password_operation,'replace');assert.strictEqual(writes[0].body.password,'synthetic-not-a-real-password');assert(!('configuration' in writes[0].body),'Credential edit must not replace the profile');assert.strictEqual(f.elements.password.value,'');
 // The handler wipes its temporary secret fields after sending; assert the wire
 // snapshot separately so the test does not depend on a retained secret object.
 assert(!w.document.querySelector('#vpn-edit'),'Successful save must return to profile list');
 w.document.querySelector('[data-vpn-edit]').click();f=w.document.querySelector('#vpn-edit');await f.onsubmit({target:f,preventDefault(){}});assert.strictEqual(writes[1].body.password_operation,'keep');assert(!('username' in writes[1].body));assert(!('password' in writes[1].body));
 w.document.querySelector('[data-vpn-edit]').click();w.document.querySelector('#vpn-cancel').click();assert(!w.document.querySelector('#vpn-edit'));await w.document.querySelector('#vpn-test').onclick();assert.strictEqual(writes[2].path,'/api/vpn/test');assert.strictEqual(writes[2].method,'POST');
 status.state='DISCONNECTED';status.traffic.collecting=false;status.traffic.completed=true;await intervals.filter(x=>x.delay===2000).at(-1).fn();assert.strictEqual(w.document.querySelector('#vpn-rx-rate').textContent,'Last completed session');assert(w.document.querySelector('#vpn-test').disabled);
 status.state='CONNECTING';status.traffic={session_id:2,available:false,collecting:false,rx_bytes:0,tx_bytes:0};status.public_ip='';status.public_ip_verified=false;status.public_ip_checked_at=0;status.verified_country='';status.verification={state:'unverified',location_state:'unverified'};await intervals.filter(x=>x.delay===2000).at(-1).fn();assert.strictEqual(w.document.querySelector('#vpn-rx-total').textContent,'Unavailable');assert.strictEqual(w.document.querySelector('#vpn-public-ip').textContent,'Unverified');assert.strictEqual(w.document.querySelector('#vpn-location-country').textContent,'Unknown');
 w.vpnPageCleanup();w.close();
 const lan=await fixture('http://192.168.1.78/');assert(lan.w.document.querySelector('[data-vpn-edit]').disabled);assert(lan.w.document.querySelector('#vpn-add').disabled);assert(lan.w.document.querySelector('a[href="https://192.168.1.78/#vpn"]'));assert(!lan.w.document.querySelector('input[type=password]'));lan.w.close();
 const tls=await fixture('https://192.168.1.78/');assert(!tls.w.document.querySelector('[data-vpn-edit]').disabled);tls.w.document.querySelector('[data-vpn-edit]').click();assert(tls.w.document.querySelector('#vpn-edit'));tls.w.close();
 console.log('VPN DOM: traffic units, IP/location display, manual refresh, two-second updates retaining editor/focus/selection, new-session invalidation and provisioning policy passed');
})().catch(e=>{console.error(e.message);process.exitCode=1});
