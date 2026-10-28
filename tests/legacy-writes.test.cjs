const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(require.resolve('../source/js/app-core.js'),'utf8');
function load(context,name) {
  const start=source.indexOf('  async function '+name+'(');
  const end=source.indexOf('\n  }',start)+4;
  vm.runInContext(source.slice(start,end),context);
}
function loadPlayerHook(context) {
  const start=source.indexOf('  // ── Explicit player/physics hook control');
  const end=source.indexOf('\n  async function uninstallPlayerHook',start);
  assert.ok(start >= 0 && end > start, 'expected the production player-hook block');
  vm.runInContext(source.slice(start,end),context);
}
function hookContext({branch='30640140', onWrite=()=>{}}={}) {
  const status={textContent:'',style:{}};
  const context=vm.createContext({ip:'console',connectionGeneration:1,srvBusy:false,log(){},
    document:{getElementById:id=>id==='player-hook-status'?status:null},
    ps3getmem:async(address,length)=>address===0x001C4110?branch:'00'.repeat(length),
    verifiedMemoryValue:onWrite});
  loadPlayerHook(context);
  return {context,status};
}
test('player hook activation is last and is skipped on an unverified payload',async()=>{
  for (const failAt of [0,2]) {
    const writes=[];
    const {context}=hookContext({onWrite:async(a,v)=>{writes.push([a,v]);if(failAt&&writes.length===failAt)throw new Error('readback mismatch');}});
    if(failAt){
      await assert.rejects(context.installPlayerHook(),/readback mismatch/);
      assert.equal(writes.some(([a])=>a===0x001C4110),false);
    } else {
      await context.installPlayerHook();
      assert.deepEqual(writes.at(-1),[0x001C4110,'48F14DB0']);
      assert.equal(writes.length,81);
    }
    assert.equal(vm.runInContext('hookInstalling',context),false);
  }
});
test('hook installation cannot continue writing in a replacement session',async()=>{
  let context,writes=0;
  const hook=hookContext({onWrite:async()=>{writes++;context.connectionGeneration++;}});
  context=hook.context;
  await assert.rejects(context.installPlayerHook(),/session changed/);assert.equal(writes,1);
});
test('hook install rejects unsupported instructions without mutating memory',async()=>{
  let writes=0;
  const {context}=hookContext({branch:'DEADBEEF',onWrite:async()=>writes++});
  await assert.rejects(context.installPlayerHook(),/supported BLES 01.24/);assert.equal(writes,0);
});
test('custom writes honor selected widths and reject overflow without sending',async()=>{
  const calls=[],nodes={'c-addr':{value:'1234'},'c-val':{value:'12'}};
  const context=vm.createContext({document:{getElementById:id=>nodes[id]},advancedMode:true,writeSize:1,
    verifiedMemoryValue:async(a,v)=>calls.push([a,v]),toast(){},log(){}});
  load(context,'sendCustom');await context.sendCustom();assert.deepEqual(calls,[['1234','12']]);
  context.writeSize=2;await context.sendCustom();assert.deepEqual(calls.at(-1),['1234','0012']);
  nodes['c-val'].value='123456';await context.sendCustom();assert.equal(calls.length,2);
});
test('reset-all reports unverified writes instead of claiming everything reset',async()=>{
  const statuses=[];
  const context=vm.createContext({connectionGeneration:1,log(){},toast(){},floatToBEHex:()=> '3F800000',dsDetectedOff:'38C60001',srvMode:'off',
    document:{getElementById:id=>({value:'',classList:{contains:()=>id==='tog-god-mode',remove(){}}})},
    patchRunner:{apply:async()=>({status:'unverified'})},requestedCheatState:{},cheatState:{},updateCheatStatus:(_,s)=>statuses.push(s)});
  const constantStart=source.indexOf('  const UNLIMITED_AMMO_PATCH =');
  const constantEnd=source.indexOf('\n  };',constantStart)+5;
  vm.runInContext(source.slice(constantStart,constantEnd),context);
  load(context,'resetAllCheats');const result=await context.resetAllCheats();
  assert.equal(result.verified,0);assert.equal(result.failed,1);assert.deepEqual(statuses,['unknown']);
});
