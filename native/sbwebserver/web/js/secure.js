/* Transport UI only. Authorization is always enforced by the backend. */
window.securePortalUrl=function(info,page='vpn'){
 if(!info.available||!Number.isInteger(info.port)||info.port<1||info.port>65535)return '';
 const u=new URL(location.href);u.protocol='https:';u.port=info.port===443?'':String(info.port);u.pathname='/';u.search='';u.hash=page;return u.href;
};
window.secureTrustHelp='Before entering a pairing code, open Standalone Base > Web Access > Device Certificate on the physical device. Inspect the certificate presented by your browser and compare all SHA-256 fingerprint groups. Continue only if they match. A self-signed warning is expected on first use; do not accept a different fingerprint. If your browser cannot show it, use a supported desktop browser.';
(async()=>{try{
 const r=await fetch('/api/secure-portal',{cache:'no-store'});if(!r.ok)return;const info=await r.json();
 const box=document.createElement('div');box.className='notice';box.id='secure-management';
 if(location.protocol==='https:'){box.textContent='Secure Web Portal: encrypted connection. '+window.secureTrustHelp;}
 else if(info.available){const a=document.createElement('a');a.className='button';a.href=window.securePortalUrl(info,location.pathname==='/remote'?'remote':'');a.textContent='Open Secure Portal';box.textContent='Secure management available. ';box.append(a);}
 else box.textContent='Secure management unavailable. Sensitive provisioning is disabled on LAN HTTP.';
 document.querySelector('header').after(box);
}catch(e){}})();
