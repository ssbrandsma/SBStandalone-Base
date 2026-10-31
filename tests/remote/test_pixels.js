const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync(process.argv[2],'utf8').replace(/\}\)\(\);\s*$/, 'window.decode8888=decode8888;window.decode565=decode565;})();');
const context={window:{},DataView,Uint8Array,Uint8ClampedArray};vm.runInNewContext(source,context);
let image;const canvas={getContext:()=>({createImageData:(w,h)=>({data:new Uint8ClampedArray(w*h*4)}),putImageData:i=>image=i.data})};
// Two rows, padding, BGRX bytes; X must be ignored rather than used as alpha.
const raw=new Uint8Array([0,0,255,0,0,255,0,7,99,99,99,99,255,0,0,42,255,255,255,1,99,99,99,99]);
context.window.decode8888(raw.buffer,{width:2,height:2,stride:12},'cw',canvas);
assert.equal(canvas.width,2);assert.equal(canvas.height,2);
assert.deepEqual([...image],[255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255]);
assert.throws(()=>context.window.decode8888(raw.buffer,{width:3,height:2,stride:8},'cw',canvas));
assert.throws(()=>context.window.decode8888(new ArrayBuffer(2),{width:2,height:2,stride:8},'cw',canvas));
context.window.decode565(new Uint8Array([0,248,31,0]).buffer,{width:1,height:2,stride:2},'cw',canvas);
assert.equal(canvas.width,2);assert.equal(canvas.height,1);
assert.deepEqual([...image],[0,0,255,255,255,0,0,255]);
console.log('Touch BGRX8888 and Baby RGB565 color/stride/orientation tests passed');
