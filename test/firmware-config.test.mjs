import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const html=readFileSync(new URL('../firmware/src/config_page.h',import.meta.url),'utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
function page(accounts,{saved='',unauthorized=false,storageBlocked=false,XMLHttpRequest,scanResults=[]}={}) {
  const elements={};
  const element=()=>({value:'',checked:false,hidden:false,children:[],replaceChildren(...v){this.children=v},add(v){this.children.push(v)}});
  for(const name of ['apiKey','clearApiKey','provider','accountId','windowId','liquidTheme','quotaIcon','usbBrightness','batteryBrightness','usbSeconds','batterySeconds','alwaysOn','wakeOnData','background','showIdentity','wifiCompatibility','levelOffsetDegrees'])elements[name]=element();
  elements.levelOffsetDegrees.type='number';
  elements.wifiCompatibility.type='checkbox';
  for(const name of ['levelOffsetRightDegrees','levelOffsetDownDegrees','levelOffsetLeftDegrees'])elements[name]={...element(),type:'number'};
  for(const name of ['accelBiasXG','accelBiasYG'])elements[name]={...element(),type:'number',value:'0'};
  const nodes=new Map([['#settings',{elements}],['#providers',elements.provider],['#wifi',{elements:{ssid:element(),password:element()}}]]);
  const get=s=>{if(!nodes.has(s))nodes.set(s,element());return nodes.get(s)};
  const response={config:{provider:'codex',accountId:'',windowId:'',usbBrightness:179,batteryBrightness:102,hasApiKey:false,wifiCompatibility:false},accounts,ssid:'Test Wi-Fi',status:{wifi:'connected',ssid:'Test Wi-Fi',screen:1,viscosity:.45,usb:true,fps:0,channels:[{},{},{}]}};
  const storage=new Map(saved?[['magpie-screen-access',saved]]:[]);
  const requests=[];
  const context=vm.createContext({localStorage:{getItem:k=>{if(storageBlocked)throw Error('blocked');return storage.get(k)},setItem:(k,v)=>{if(storageBlocked)throw Error('blocked');storage.set(k,v)},removeItem:k=>storage.delete(k)},document:{querySelector:get,querySelectorAll:()=>[]},Option:class{constructor(label,value){this.label=label;this.value=value}},setInterval(){},setTimeout:resolve=>resolve(),FormData:class{},fetch:async(path,options)=>{requests.push({path,options});return {ok:!unauthorized,status:unauthorized?401:200,json:async()=>path==='/api/wifi/scan'?(options.method==='POST'?{state:'scanning'}:scanResults.shift()||{state:'scanning'}):response}}});
  context.XMLHttpRequest=XMLHttpRequest;
  context.FormData=class {set(){}};
  vm.runInContext(script,context);return {context,get,elements,storage,requests,response};
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

test('provider choices include discovered providers and preserve the saved selection',async()=>{
  const rows=[
    {id:'c',provider:'codex',label:'Codex',windows:[{id:'weekly',name:'Weekly'}]},
    {id:'a',provider:'claude',label:'Claude',windows:[{id:'daily',name:'Daily'}]},
    {id:'g',provider:'gemini',label:'Gemini',windows:[]},
  ];
  const {context,elements,response}=page(rows);
  Object.assign(response.config,{provider:'claude',accountId:'a',windowId:'daily'});
  await vm.runInContext('load()',context);
  assert.deepEqual(elements.provider.children.map(o=>o.value),['codex','claude','gemini']);
  assert.equal(elements.provider.value,'claude');
  assert.equal(elements.accountId.value,'a');
  assert.equal(elements.windowId.value,'daily');
  elements.provider.value='codex';elements.provider.onchange();
  assert.equal(elements.accountId.children[1].value,'c');
  assert.equal(elements.windowId.children[1].value,'weekly');
  assert.equal(elements.accountId.value,'');
  assert.equal(elements.windowId.value,'');
  response.config.provider='saved-provider';response.accounts=[];
  await vm.runInContext('load()',context);
  assert.equal(elements.provider.value,'saved-provider');
  assert.ok(elements.provider.children.some(o=>o.value==='saved-provider'));
});

test('appearance selectors load and save independently without changing remote quota selection',async()=>{
  const {context,elements,requests,response}=page([]);
  Object.assign(response.config,{provider:'remote-magpie/claude',accountId:'remote-account',windowId:'daily',liquidTheme:'gemini',quotaIcon:'claude'});
  await vm.runInContext('load()',context);
  assert.equal(elements.liquidTheme.value,'gemini');
  assert.equal(elements.quotaIcon.value,'claude');
  elements.liquidTheme.value='codex';elements.quotaIcon.value='gemini';
  elements[Symbol.iterator]=function*(){for(const [name,e] of Object.entries(this))yield {...e,name}};
  await vm.runInContext('form.onsubmit({preventDefault(){}})',context);
  await new Promise(resolve=>setImmediate(resolve));
  const body=JSON.parse(requests.find(r=>r.options.method==='POST'&&r.path==='/api/config').options.body);
  assert.equal(body.config.liquidTheme,'codex');
  assert.equal(body.config.quotaIcon,'gemini');
  assert.equal(body.config.provider,'remote-magpie/claude');
  assert.equal(body.config.accountId,'remote-account');
  assert.equal(body.config.windowId,'daily');
  delete response.config.liquidTheme;delete response.config.quotaIcon;
  await vm.runInContext('load()',context);
  assert.equal(elements.liquidTheme.value,'auto');
  assert.equal(elements.quotaIcon.value,'auto');
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
test('Wi-Fi failure displays the reason and recovers after a retry connects',async()=>{
  const {context,get}=page([]);
  await vm.runInContext("showWifi({wifi:'failed',ssid:'Test Wi-Fi',wifiReason:201,wifiReasonName:'NO_AP_FOUND',wifiError:'找不到指定的 2.4 GHz 热点'})",context);
  assert.match(get('#wifiStatus').textContent,/连接失败.*Test Wi-Fi.*找不到.*201.*NO_AP_FOUND/);
  assert.doesNotMatch(get('#wifiStatus').textContent,/正在连接/);
  assert.equal(get('#wifi').hidden,false);
  await vm.runInContext("showWifi({wifi:'failed',ssid:'Test Wi-Fi',wifiReason:0,wifiError:'已连接热点，但获取 IP 超时，请检查路由器 DHCP'})",context);
  assert.match(get('#wifiStatus').textContent,/获取 IP 超时/);
  assert.doesNotMatch(get('#wifiStatus').textContent,/错误码/);
  await vm.runInContext("showWifi({wifi:'connecting',ssid:'Test Wi-Fi'})",context);
  assert.equal(get('#wifiStatus').textContent,'正在连接 Wi-Fi：Test Wi-Fi');
  await vm.runInContext("showWifi({wifi:'connected',ssid:'Test Wi-Fi'})",context);
  assert.equal(get('#wifiStatus').textContent,'已连接 Wi-Fi：Test Wi-Fi');
  assert.equal(get('#wifi').hidden,true);
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

test('level calibration loads and saves signed fractional degrees as a number',async()=>{
  for(const offset of [-8.3,0,12.5]) {
    const {context,elements,requests,response}=page([]);
    response.config.levelOffsetDegrees=-2.4;
    await vm.runInContext('load()',context);
    assert.equal(Number(elements.levelOffsetDegrees.value),-2.4);
    elements.levelOffsetDegrees.value=String(offset);
    elements[Symbol.iterator]=function*(){for(const [name,e] of Object.entries(this))yield {...e,name}};
    await vm.runInContext('form.onsubmit({preventDefault(){}})',context);
    await new Promise(resolve=>setImmediate(resolve));
    const body=JSON.parse(requests.find(r=>r.options.method==='POST'&&r.path==='/api/config').options.body);
    assert.equal(body.config.levelOffsetDegrees,offset);
  }
});

test('four directional calibration values load and save independently',async()=>{
  const {context,elements,requests,response}=page([]);
  // Distinct synthetic values detect accidental coupling between direction fields.
  const calibration={levelOffsetDegrees:-2.5,levelOffsetRightDegrees:1.25,levelOffsetDownDegrees:4,levelOffsetLeftDegrees:-1.5};
  Object.assign(response.config,calibration);
  await vm.runInContext('load()',context);
  for(const [key,value] of Object.entries(calibration))assert.equal(Number(elements[key].value),value);
  elements.levelOffsetRightDegrees.value='-2.5';
  elements[Symbol.iterator]=function*(){for(const [name,e] of Object.entries(this))yield {...e,name}};
  await vm.runInContext('form.onsubmit({preventDefault(){}})',context);
  await new Promise(resolve=>setImmediate(resolve));
  const body=JSON.parse(requests.find(r=>r.options.method==='POST'&&r.path==='/api/config').options.body);
  for(const [key,value] of Object.entries(calibration))assert.equal(body.config[key],key==='levelOffsetRightDegrees'?-2.5:value);
});

test('sensor axis bias values returned by the device retain full decimal precision on save',async()=>{
  const {context,elements,requests,response}=page([]);
  // Synthetic signed decimals test precision, not a particular device calibration.
  const syntheticBias={x:.073307998,y:-.058463,editedY:.045678123};
  Object.assign(response.config,{accelBiasXG:syntheticBias.x,accelBiasYG:syntheticBias.y,levelOffsetDegrees:0});
  await vm.runInContext('load()',context);
  assert.equal(Number(elements.accelBiasXG.value),syntheticBias.x);
  assert.equal(Number(elements.accelBiasYG.value),syntheticBias.y);
  elements.accelBiasYG.value=String(syntheticBias.editedY);
  elements[Symbol.iterator]=function*(){for(const [name,e] of Object.entries(this))yield {...e,name}};
  await vm.runInContext('form.onsubmit({preventDefault(){}})',context);
  await new Promise(resolve=>setImmediate(resolve));
  const body=JSON.parse(requests.find(r=>r.options.method==='POST'&&r.path==='/api/config').options.body);
  assert.equal(body.config.accelBiasXG,syntheticBias.x);
  assert.equal(body.config.accelBiasYG,syntheticBias.editedY);
  assert.equal(body.config.levelOffsetDegrees,0);
});

test('OTA upload sends the image size and waits for device validation after 100% transfer',async()=>{
  let xhr;
  class Upload {
    constructor(){xhr=this;this.upload={};this.headers={}}
    open(method,path){this.method=method;this.path=path}
    setRequestHeader(k,v){this.headers[k]=v}
    send(){}
  }
  const {context,get}=page([],{XMLHttpRequest:Upload});
  const pending=vm.runInContext('uploadFirmware({size:2048})',context);
  let resolved=false;pending.then(()=>resolved=true);
  assert.equal(xhr.path,'/update');
  assert.equal(xhr.headers['X-Firmware-Size'],'2048');
  xhr.upload.onprogress({lengthComputable:true,loaded:1024,total:2048});
  assert.equal(get('#otaBar').value,50);
  xhr.upload.onprogress({lengthComputable:true,loaded:2048,total:2048});
  assert.match(get('#otaStage').textContent,/等待固件校验/);
  await Promise.resolve();assert.equal(resolved,false);
  xhr.status=200;xhr.responseText='{"ok":true}';xhr.onload();
  await pending;assert.equal(resolved,true);
});

test('OTA rejects validation errors, lost connections and timeouts',async()=>{
  for(const failure of ['validation','network','timeout','unauthorized']) {
    let xhr;
    class Upload {
      constructor(){xhr=this;this.upload={}}
      open(){} setRequestHeader(){} send(){}
    }
    const {context,get}=page([],{XMLHttpRequest:Upload});
    const pending=vm.runInContext('uploadFirmware({size:2048})',context);
    const rejected=assert.rejects(pending);
    if(failure==='network')xhr.onerror();
    else if(failure==='timeout')xhr.ontimeout();
    else {xhr.status=failure==='unauthorized'?401:400;xhr.responseText='{"error":"Rejected"}';xhr.onload()}
    await rejected;
    if(failure==='unauthorized')assert.equal(get('#content').hidden,true);
  }
});

test('Wi-Fi compatibility loads and saves booleans without fetching during reconnect',async()=>{
  assert.match(html, /name="wifiCompatibility" type="checkbox" form="settings"/);
  for (const enabled of [true,false]) {
    const {context,elements,requests,response,get}=page([]);
    response.config.wifiCompatibility=!enabled;
    await vm.runInContext('load()',context);
    assert.equal(elements.wifiCompatibility.checked,!enabled);
    elements.wifiCompatibility.checked=enabled;
    elements[Symbol.iterator]=function*(){for(const [name,e] of Object.entries(this))yield {...e,name}};
    await vm.runInContext('form.onsubmit({preventDefault(){}})',context);
    await new Promise(resolve=>setImmediate(resolve));
    const body=JSON.parse(requests.find(r=>r.options.method==='POST'&&r.path==='/api/config').options.body);
    assert.equal(body.config.wifiCompatibility,enabled);
    assert.equal(requests.filter(r=>r.path==='/api/config'&&r.options.method==='GET').length,1);
    assert.match(get('#message').textContent,/已保存.*重新连接/);
  }
});

test('scanning Wi-Fi populates choices without changing typed credentials; selection and manual input both work',async()=>{
  const rows=[{ssid:'Nearby <network>',rssi:-40,authName:'WPA2-Personal'},{ssid:'Another network',rssi:-65,authName:'Open'}];
  const {context,get,requests}=page([],{scanResults:[{state:'scanning'},{state:'done',networks:rows}]});
  const fields=get('#wifi').elements;
  fields.ssid.value='Hidden network';fields.password.value='typed-password';
  await vm.runInContext('scanWifi()',context);
  assert.equal(get('#wifiNetworks').children.length,3);
  assert.equal(get('#wifiNetworks').children[1].value,rows[0].ssid);
  assert.match(get('#wifiNetworks').children[1].label,/Nearby <network>.*WPA2-Personal.*-40/);
  assert.equal(fields.ssid.value,'Hidden network');assert.equal(fields.password.value,'typed-password');
  assert.equal(requests.filter(r=>r.path==='/api/wifi').length,0,'scanning must not save or connect');
  get('#wifiNetworks').value=rows[0].ssid;get('#wifiNetworks').onchange();
  assert.equal(fields.ssid.value,rows[0].ssid);assert.equal(fields.password.value,'');
  fields.ssid.value='Manual hidden SSID';fields.ssid.oninput();
  assert.equal(get('#wifiNetworks').value,'');
  fields.password.value='manual-password';
  get('#wifi').onsubmit({preventDefault(){},target:get('#wifi')});
  await new Promise(resolve=>setImmediate(resolve));
  const body=JSON.parse(requests.find(r=>r.path==='/api/wifi').options.body);
  assert.equal(body.ssid,'Manual hidden SSID');assert.equal(body.password,'manual-password');
});

test('empty, failed and timed-out Wi-Fi scans leave manual input available',async()=>{
  for(const result of [{state:'done',networks:[]},{state:'failed',error:'扫描失败，请手动输入'},{state:'scanning'}]){
    const {context,get}=page([],{scanResults:[result]});
    get('#wifi').elements.ssid.value='Typed network';get('#wifi').elements.password.value='keep-password';
    if(result.state==='done')await vm.runInContext('scanWifi()',context);
    else await assert.rejects(vm.runInContext('scanWifi()',context));
    assert.match(get('#scanWifiStatus').textContent,/手动输入/);
    assert.equal(get('#wifi').elements.ssid.value,'Typed network');
    assert.equal(get('#wifi').elements.password.value,'keep-password');
  }
});
