import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const html=readFileSync(new URL('../firmware/src/config_page.h',import.meta.url),'utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
function page(accounts,{saved='',unauthorized=false,storageBlocked=false}={}) {
  const elements={};
  const element=()=>({value:'',checked:false,hidden:false,children:[],replaceChildren(...v){this.children=v},add(v){this.children.push(v)}});
  for(const name of ['apiKey','clearApiKey','provider','accountId','windowId','usbBrightness','batteryBrightness','usbSeconds','batterySeconds','alwaysOn','wakeOnData','background','showIdentity'])elements[name]=element();
  const nodes=new Map([['#settings',{elements}],['#wifi',{elements:{ssid:element(),password:element()}}]]);
  const get=s=>{if(!nodes.has(s))nodes.set(s,element());return nodes.get(s)};
  const response={config:{provider:'codex',accountId:'',windowId:'',usbBrightness:179,batteryBrightness:102,hasApiKey:false},accounts,ssid:'Test Wi-Fi',status:{wifi:'connected',ssid:'Test Wi-Fi',screen:1,viscosity:.45,usb:true,fps:0,channels:[{},{},{}]}};
  const storage=new Map(saved?[['magpie-screen-access',saved]]:[]);
  const requests=[];
  const context=vm.createContext({localStorage:{getItem:k=>{if(storageBlocked)throw Error('blocked');return storage.get(k)},setItem:(k,v)=>{if(storageBlocked)throw Error('blocked');storage.set(k,v)},removeItem:k=>storage.delete(k)},document:{querySelector:get,querySelectorAll:()=>[]},Option:class{constructor(label,value){this.label=label;this.value=value}},setInterval(){},FormData:class{},fetch:async(path,options)=>{requests.push({path,options});return {ok:!unauthorized,status:unauthorized?401:200,json:async()=>response}}});
  vm.runInContext(script,context);return {context,get,elements,storage,requests};
}
test('device config opens before the first account snapshot, including a null response',async()=>{
  for(const accounts of [null,[],undefined]) {
    const {context,get,elements}=page(accounts);
    await vm.runInContext('load()',context);
    assert.equal(elements.accountId.children.length,1);
    assert.equal(elements.windowId.children.length,1);
    assert.equal(get('#providers').children.length,2);
    assert.equal(get('#secretState').textContent,'尚未保存 API Key');
  }
});
test('device config preserves account/window selection and clears entered secrets after save',async()=>{
  const {context,elements}=page([{id:'a',provider:'codex',label:'Account',windows:[{id:'Weekly:0',name:'Weekly'}]}]);
  elements.apiKey.value='entered-key';
  await vm.runInContext('load()',context);
  assert.equal(elements.accountId.children.length,2);
  assert.equal(elements.windowId.children.length,2);
  assert.equal(elements.apiKey.value,'');
});

test('connected Wi-Fi shows the current network and an explicit switch/cancel flow',async()=>{
  const {context,get}=page([]);
  await vm.runInContext('load()',context);
  assert.equal(get('#wifiStatus').textContent,'已连接 Wi-Fi：Test Wi-Fi');
  assert.equal(get('#wifi').hidden,true);
  get('#switchWifi').onclick();
  assert.equal(get('#wifi').hidden,false);
  await vm.runInContext("showStatus({wifi:'connected',ssid:'Test Wi-Fi',usb:true,fps:0,channels:[{},{},{}]})",context);
  assert.equal(get('#wifi').hidden,false,'polling must not close an active network form');
  get('#wifi').elements.password.value='unsaved';
  get('#cancelWifi').onclick();
  assert.equal(get('#wifi').hidden,true);
  assert.equal(get('#wifi').elements.password.value,'');
  await vm.runInContext("showWifi({wifi:'connecting',ssid:'New network'})",context);
  assert.equal(get('#wifi').hidden,false);
});
test('a successful login persists across visits; forgetting and 401 remove it',async()=>{
  const {context,storage,get}=page([]);
  await vm.runInContext("connect('test-access')",context);
  assert.equal(storage.get('magpie-screen-access'),'test-access');
  assert.equal(get('#content').hidden,false);
  const revisit=page([],{saved:storage.get('magpie-screen-access')});
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(revisit.get('#login').hidden,true);
  assert.equal(revisit.requests[0].options.headers['X-Magpie-Key'],'test-access');
  revisit.get('#forget').onclick();
  assert.equal(revisit.storage.size,0);
  assert.equal(revisit.get('#content').hidden,true);
  const expired=page([],{saved:'expired',unauthorized:true});
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(expired.storage.size,0);
  assert.equal(expired.get('#login').hidden,false);
});
test('blocked browser storage still allows manual configuration',async()=>{
  const {context,get}=page([],{storageBlocked:true});
  await vm.runInContext("connect('test-access')",context);
  assert.equal(get('#content').hidden,false);
});
test('saving settings leaves physical page selection alone',async()=>{
  const {context,elements,requests}=page([]);
  elements[Symbol.iterator]=function*(){for(const [name,e] of Object.entries(this))yield {...e,name}};
  await vm.runInContext('load()',context);
  await vm.runInContext('form.onsubmit({preventDefault(){}})',context);
  await new Promise(resolve=>setImmediate(resolve));
  const body=JSON.parse(requests.find(r=>r.options.method==='POST'&&r.path==='/api/config').options.body);
  assert.equal('screen' in body,false);
  assert.equal(body.viscosity,.45);
  assert.doesNotMatch(html,/<select id="screen"/);
});

test('real account checkbox is saved as a boolean',async()=>{
 const {context,elements,requests}=page([]);
 elements.showIdentity.type='checkbox';elements.showIdentity.checked=true;
 elements[Symbol.iterator]=function*(){for(const [name,e] of Object.entries(this))yield {...e,name}};
 await vm.runInContext('form.onsubmit({preventDefault(){}})',context);
 await new Promise(resolve=>setImmediate(resolve));
 const body=JSON.parse(requests.find(r=>r.options.method==='POST'&&r.path==='/api/config').options.body);
 assert.equal(body.config.showIdentity,true);
});
