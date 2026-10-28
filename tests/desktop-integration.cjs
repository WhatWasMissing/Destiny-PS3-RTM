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
    replaceChildren(...children){this.children=children;this.innerHTML='';},
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
  get('ip').value='192.168.1.100';
  await context.quickReconnectFromBadge();
  assert.equal(get('status-badge').classList.contains('connected'),true);
  await context.ps3fetch('http://192.168.1.100/setmem.ps3mapi?addr=1254&val=AABBCCDD');
  await context.ps3writeU16(0x1256,0x1122);
  assert.equal(await context.ps3getmem(0x1254,4),'AABB1122','unaligned UInt16 write changed neighboring bytes');
  get('rtm-launch-iso-path').value='/dev_hdd0/PS3ISO/Destiny.iso';
  await context.launchDestiny();
  assert.match(get('rtm-launch-status').textContent,/Destiny start is not confirmed/);
  await assert.rejects(context.ps3fetch('http://192.168.0.63/setmem.ps3mapi?addr=1254&val=00000000'),/active game session/);
  await assert.rejects(context.ps3fetch('http://192.168.1.100/setmem.ps3mapi?addr=1254&val=00000000&proc=77'),/active game session/);
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
  // The fresh session has no verified Server Cheats mode. Reset All must keep
  // that state unknown when the shared hook branch does not match a known site.
  assert.equal(reset.verified,1,`unexpected reset result; ${get('log').children.map(node=>node.innerHTML).join(' | ')}`);
  assert.equal(reset.failed,1,`unexpected reset result; ${get('log').children.map(node=>node.innerHTML).join(' | ')}`);
  assert.match(get('log').children.at(-2).innerHTML,/Shared hook site has an unexpected instruction/);
  for (const [address,value] of ammoOff) assert.equal(await context.ps3getmem(parseInt(address,16),value.length/2),value);
  assert.equal(await context.ps3getmem(0x00310F94,4),'00000000','the stale single-site ammo address must not be touched');
  const hookSite=0x001C4110, hookBase=0x010D8EC0;
  const hookRegionBytes=vm.runInContext('SHARED_HOOK_REGION_BYTES',context);
  assert.equal(hookRegionBytes,332,'shared hook preflight must cover the largest known payload');
  await context.verifiedMemoryValue(hookSite,'30640140');
  assert.equal(await context.ps3getmem(hookSite,4),'30640140','hook-site fixture must be present before server-mode integration');
  await context.verifiedMemoryValue(hookBase+hookRegionBytes,'A1B2C3D4');
  const assertHookPayload=async payload=>{
    assert.equal(await context.ps3getmem(hookSite,4),'48F14DB0','shared hook must activate after the payload is written');
    assert.equal(await context.ps3getmem(hookBase,hookRegionBytes),payload.padEnd(hookRegionBytes*2,'0'));
    assert.equal(await context.ps3getmem(hookBase+hookRegionBytes,4),'A1B2C3D4','hook writer touched beyond its verified region');
  };
  const voidHook=vm.runInContext('VOID_ON_HEX',context);
  const ropkHook=vm.runInContext('ROPK_ON_HEX',context);
  const offHook=vm.runInContext('OFF_HEX',context);
  assert.equal(voidHook.length/2,292);
  assert.equal(ropkHook.length/2,332);
  assert.equal(offHook.length/2,116);
  assert.equal(await context.srvSetMode('void',get('srv-void')),true,`Void should install against the initialized hook and blank cave; ${get('log').children.map(node=>node.innerHTML).join(' | ')}`);
  await assertHookPayload(voidHook);
  assert.equal(await context.srvSetMode('ropk',get('srv-ropk')),true,`switching from the long Void payload must recognize its complete cave; ${get('log').children.map(node=>node.innerHTML).join(' | ')}`);
  await assertHookPayload(ropkHook);
  assert.equal(await context.srvSetMode('off',get('srv-off')),true,'Off must recognize and replace the full Relative OPK payload');
  await assertHookPayload(offHook);
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
