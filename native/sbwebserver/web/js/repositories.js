/* XML repositories are managed independently from the release catalog. */
async function mountXmlRepositories() {
 const section=document.createElement('section');section.className='card';
 app.querySelector('h2').after(section);
 const endpoint='/api/applets/repositories';
 async function render(){
  const data=await load(endpoint);
  section.innerHTML='<h3>XML repositories</h3><p>Add a public extensions.xml URL or upload a static XML catalog. Packages are checked before being served to the Applet Installer.</p><div id="xml-status" role="status"></div><div class="table-wrap"><table><thead><tr><th>Name</th><th>Source</th><th>Status</th><th>Applets</th><th>Actions</th></tr></thead><tbody>'+data.repositories.map((r,i)=>'<tr><td>'+esc(r.name)+'</td><td>'+esc(r.url||r.filename)+'</td><td>'+esc(r.status)+(r.last_error?': '+esc(r.last_error):'')+'</td><td>'+r.compatible+' compatible / '+r.imported+' imported</td><td><button data-rename="'+i+'" '+(r.busy?'disabled':'')+'>Rename</button> '+(r.type==='url'?'<button data-refresh="'+i+'" '+(r.busy?'disabled':'')+'>Refresh</button> ':'')+'<button data-delete="'+i+'" '+(r.busy?'disabled':'')+'>Remove</button></td></tr>').join('')+'</tbody></table></div><form id="xml-url"><h4>Add repository URL</h4><label>Name <input name="name" maxlength="128" required></label> <label>URL <input name="url" type="url" maxlength="512" required placeholder="https://example.com/extensions.xml"></label> <button>Add URL</button></form><form id="xml-upload"><h4>Upload XML catalog</h4><label>Name <input name="name" maxlength="128" required></label> <label>XML <input name="xml" type="file" accept=".xml,text/xml,application/xml" required></label> <label>Base URL for relative package links <input name="base" type="url" maxlength="512" placeholder="https://example.com/extensions.xml"></label> <button>Upload XML</button></form><button id="xml-reload">Update status</button>';
  const message=x=>{section.querySelector('#xml-status').textContent=x};
  async function run(fn){try{await fn();await applets()}catch(e){message(e.message)}}
  section.querySelector('#xml-reload').onclick=()=>run(async()=>{});
  section.querySelectorAll('[data-delete]').forEach(b=>b.onclick=()=>{const r=data.repositories[+b.dataset.delete];if(confirm('Remove '+r.name+' and its catalog entries?'))run(()=>write(endpoint+'/'+r.id,{method:'DELETE'}))});
  section.querySelectorAll('[data-refresh]').forEach(b=>b.onclick=()=>run(()=>write(endpoint+'/'+data.repositories[+b.dataset.refresh].id+'/refresh',{method:'POST'})));
  section.querySelectorAll('[data-rename]').forEach(b=>b.onclick=()=>{const r=data.repositories[+b.dataset.rename],name=prompt('Repository name',r.name);if(name)run(()=>write(endpoint+'/'+r.id,{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({name})}))});
  section.querySelector('#xml-url').onsubmit=e=>{e.preventDefault();const f=e.target;run(()=>write(endpoint,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:f.elements.name.value,url:f.elements.url.value})}))};
  section.querySelector('#xml-upload').onsubmit=e=>{e.preventDefault();const f=e.target,file=f.elements.xml.files[0];if(file.size>data.xml_limit){message('XML exceeds 256 KiB limit');return}const q=new URLSearchParams({name:f.elements.name.value,filename:file.name,base_url:f.elements.base.value});run(()=>write(endpoint+'/upload?'+q,{method:'POST',headers:{'Content-Type':'application/xml'},body:file}))};
 }
 await render();
}
