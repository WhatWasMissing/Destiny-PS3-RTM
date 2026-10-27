// Called by the Go integration test. Every request must go to its local host.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const nativeSetTimeout = setTimeout;
const origin = process.env.RTM_TEST_ORIGIN;
if (!origin) throw new Error('Run this script through go test');
function element(id = '') {
  const node = {id,value:'',className:'',style:{setProperty(){}},dataset:{},children:[],textContent:'',innerHTML:'',
    appendChild(child){this.children.push(child);},append(...children){this.children.push(...children);},
    addEventListener(){},setAttribute(){},querySelector(){return null;},remove(){},focus(){}};
  node.classList = {
    contains: name => node.className.split(/\s+/).includes(name),
    add: name => { if(!node.classList.contains(name))node.className += ' '+name; },
    remove: name => { node.className=node.className.split(/\s+/).filter(v=>v!==name).join(' '); },
    toggle: (name,on) => {if(on??!node.classList.contains(name))node.classList.add(name);else node.classList.remove(name);}
  };
  return node;
}
const nodes=new Map();
const get=id=>{if(!nodes.has(id))nodes.set(id,element(id));return nodes.get(id);};
const storage=new Map();
const context=vm.createContext({
  URL,Response,AbortController,AbortSignal,DOMException,TextEncoder,TextDecoder,
  setTimeout:(callback,ms,...args)=>nativeSetTimeout(callback,[3000,1000].includes(ms)?1:ms,...args),clearTimeout,setInterval:()=>1,clearInterval(){},console,
  location:{hostname:'127.0.0.1'},CSS:{escape:value=>value},
  localStorage:{getItem:key=>storage.get(key)??null,setItem:(key,value)=>storage.set(key,value),removeItem:key=>storage.delete(key)},
  sessionStorage:{getItem:()=>null,setItem(){}},
  document:{body:element(),documentElement:element(),getElementById:get,createElement:()=>element(),
    addEventListener(){},querySelectorAll(selector){const id=selector.match(/^\[id="([^"]+)"\]$/)?.[1];return id?[get(id)]:[];}},
  addEventListener(){},
  fetch:async(input,options)=>{
    const url=new URL(input,origin);
    assert.equal(url.origin,origin,'Frontend attempted a request outside the isolated fake host');
    return fetch(url,options);
  }
});
context.window=context;
for(const file of ['desktop-host','transport','operations','backups','connection','build-compatibility','screenshot-tools','app-core','launch-tools']) {
  vm.runInContext(fs.readFileSync(`source/js/${file}.js`,'utf8'),context,{filename:file+'.js'});
}
(async()=>{
  get('ip').value='192.168.0.62';
  await context.quickReconnectFromBadge();
  assert.equal(get('status-badge').classList.contains('connected'),true);
  await context.ps3fetch('http://192.168.0.62/setmem.ps3mapi?addr=1254&val=AABBCCDD');
  await context.ps3writeU16(0x1256,0x1122);
  assert.equal(await context.ps3getmem(0x1254,4),'AABB1122','unaligned UInt16 write changed neighboring bytes');
  get('rtm-launch-iso-path').value='/dev_hdd0/PS3ISO/Destiny.iso';
  await context.launchDestiny();
  assert.match(get('rtm-launch-status').textContent,/Destiny start is not confirmed/);
  await assert.rejects(context.ps3fetch('http://192.168.0.63/setmem.ps3mapi?addr=1254&val=00000000'),/active game session/);
  await assert.rejects(context.ps3fetch('http://192.168.0.62/setmem.ps3mapi?addr=1254&val=00000000&proc=77'),/active game session/);
  const button=get('integration-toggle');
  await context.togFire('1234','AABBCCDD','00000000','Integration toggle',button);
  assert.equal(vm.runInContext("cheatRuntimeStatus['integration-toggle']",context),'verified-on');
  await context.togFire('1234','AABBCCDD','00000000','Integration toggle',button);
  assert.equal(vm.runInContext("cheatRuntimeStatus['integration-toggle']",context),'verified-off');
  const ammo = get('tog-ammo');
  const ammoOn = [['003108EC','3880270F'],['00310554','38A0270F'],['00CF95AC','60000000FC1EF8003FC0461C63DE3C0093DF0110']];
  const ammoOff = [['003108EC','63C40000'],['00310554','63C50000'],['00CF95AC','63DD0000408200083BA00001807F001CD3DF0110']];
  await context.toggleUnlimitedAmmo(ammo);
  for (const [address,value] of ammoOn) assert.equal(await context.ps3getmem(parseInt(address,16),value.length/2),value);
  await context.toggleUnlimitedAmmo(ammo);
  for (const [address,value] of ammoOff) assert.equal(await context.ps3getmem(parseInt(address,16),value.length/2),value);
  await context.toggleUnlimitedAmmo(ammo);
  const reset = await context.resetAllCheats();
  assert.equal(reset.failed,0);
  for (const [address,value] of ammoOff) assert.equal(await context.ps3getmem(parseInt(address,16),value.length/2),value);
  assert.equal(await context.ps3getmem(0x00310F94,4),'00000000','the stale single-site ammo address must not be touched');
  const mismatch=get('integration-mismatch');
  await context.togFire('1238','AABBCCDD','00000000','Mismatch',mismatch);
  assert.equal(vm.runInContext("cheatRuntimeStatus['integration-mismatch']",context),'unknown');
  const partial=get('integration-partial');
  await context.togFireMulti([['1240','AABBCCDD'],['BADBAD','00000001']],[], 'Partial',partial);
  assert.equal(vm.runInContext("cheatRuntimeStatus['integration-partial']",context),'unknown');
  await context.disconnect();
  assert.equal(get('status-badge').textContent,'OFFLINE');
  assert.equal(get('integration-toggle').classList.contains('active-cheat'),false);
  const diagnostics=vm.runInContext('consoleTransport.diagnostics()',context);
  assert.ok(diagnostics.some(entry=>entry.operation==='/setmem.ps3mapi'&&entry.outcome==='failed'));
  console.log('Desktop integration passed: host detection/auth, RTM launch stages, write/readback, mismatch, partial failure, disconnect, diagnostics.');
})().catch(error=>{console.error(error);process.exitCode=1;});
