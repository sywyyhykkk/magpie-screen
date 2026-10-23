"""Explicit on-board integration test, using a temporary synthetic Magpie server.
Run after flashing, before setting a real API key. Reads existing local pairing token
without printing it. Restores the original non-secret config and removes the test key.
"""
import json, os, socket, threading, time, urllib.request, urllib.error
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
host = os.environ.get('DEVICE_HOST')
if not host:
    raise SystemExit('Set DEVICE_HOST to the test board hostname or LAN IP')
key = os.environ.get('DEVICE_KEY') or json.loads((ROOT/'.local/device-config.json').read_text())['token']
sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.connect((host,80));lan=sock.getsockname()[0];sock.close()
fixture={'remaining':65,'quotaFail':False,'route':None,'seq':1,'calls':0}
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def do_GET(self):
        if self.headers.get('Authorization') != 'Bearer standalone-fixture':
            self.send_error(401);return
        if self.path.startswith('/v1/magpie/quotas'):
            if fixture['quotaFail']: self.send_error(503);return
            d={'data':[{'provider':'codex','name':'Test','kind':'subscription','user':'fixture','readAt':str(time.time()),'windows':[{'name':'Weekly','remaining':fixture['remaining']}]}]}
        elif self.path.startswith('/v1/magpie/usage'):
            d={'calls':fixture['calls'],'errors':0,'input':1000,'output':200,'cache_read':100,'cache_write':50,'cost':0.01,'models':[{'provider':'codex','input':1000,'output':200}]}
        elif self.path.startswith('/v1/magpie/trace'):
            r=fixture['route'];d={'pid':1,'seq':fixture['seq'],'ids':[r['id']] if r else [],'routes':[r] if r else [],'now':str(time.time())}
        else:self.send_error(404);return
        b=json.dumps(d).encode();self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(b)));self.end_headers();self.wfile.write(b)
def request(path,body=None,token=key):
    req=urllib.request.Request('http://'+host+path,data=None if body is None else json.dumps(body).encode(),headers={'X-Magpie-Key':token,'Content-Type':'application/json'})
    with urllib.request.urlopen(req,timeout=8) as r:return json.load(r)
def status():return request('/api/status')
def wait(predicate,timeout=18):
    end=time.time()+timeout;last=None
    while time.time()<end:
        last=status()
        if predicate(last):return last
        time.sleep(.5)
    raise AssertionError('Timed out: '+json.dumps(last))
def refresh():request('/api/refresh',{})
def save(c,**extra):return request('/api/config',{'config':c,**extra})
old=request('/api/config');assert not old['config']['hasApiKey'], 'Configure test before a real API key; never overwrite user credentials'
server=ThreadingHTTPServer((lan,0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    try:request('/api/config',token='wrong');raise AssertionError('auth accepted')
    except urllib.error.HTTPError as e:assert e.code==401
    c={k:v for k,v in old['config'].items() if k!='hasApiKey'}
    c.update(url=f'http://{lan}:{server.server_port}',apiKey='standalone-fixture',provider='codex',accountId='',windowId='',usbSeconds=30,alwaysOn=False,wakeOnData=True)
    save(c,screen=1,viscosity=1)
    st=wait(lambda s:s['remaining']==65 and all(c['ok'] for c in s['channels']))
    assert st['standalone'] and st['usb']
    current=request('/api/config');assert current['config']['hasApiKey'] and 'apiKey' not in current['config']
    assert current['accounts'][0]['windows'][0]['id']=='Weekly:0'
    # The prior test / physical tilt may leave a real slosh animation settling.
    deadline=time.time()+25
    while True:
        before=status();time.sleep(3);after=status()
        if after['renderedFrames']==before['renderedFrames']:break
        assert time.time()<deadline, 'liquid did not settle (keep board stationary)'
    before=after;refresh();time.sleep(2);after=status()
    assert after['idleSeconds']>=before['idleSeconds'], 'identical polling counted as activity'
    assert after['renderedFrames']==before['renderedFrames'], 'unchanged metadata repainted a settled screen'
    print('PASS direct fetch, account mapping, authentication, secret redaction, identical polling / stopped redraw',flush=True)
    fixture['remaining']=42;refresh();st=wait(lambda s:s['remaining']==42);assert st['idleSeconds']<3
    fixture['quotaFail']=True;refresh();st=wait(lambda s:not s['channels'][0]['ok']);assert st['remaining']==42
    time.sleep(3);before=status();fixture['quotaFail']=False;fixture['remaining']=80;refresh();st=wait(lambda s:s['remaining']==80 and s['channels'][0]['ok']);assert st['idleSeconds']>=before['idleSeconds']
    print('PASS quota changes, stale reading retained, reconnect baseline suppresses old activity',flush=True)
    fixture['route']={'id':10,'done':False,'status':0,'provider':'codex','model':'test','tries':[{'id':'seat','model':'test','done':False}],'order':[{'id':'seat','provider':'codex'}]};fixture['seq']+=1
    refresh();wait(lambda s:s['idleSeconds']<2)
    time.sleep(3)
    fixture['route']['done']=True;fixture['route']['status']=200;fixture['route']['tries'][0]['done']=True;fixture['seq']+=1
    refresh();wait(lambda s:s['idleSeconds']<2)
    print('PASS new route and completion count as activity',flush=True)
    try:save({'usbBrightness':200,'batterySeconds':0});raise AssertionError('invalid config accepted')
    except urllib.error.HTTPError as e:assert e.code==400
    assert request('/api/config')['config']['usbBrightness']==c['usbBrightness']
    try:request('/command',{'op':'state','state':{'quota':{'remaining':99}}});raise AssertionError('bridge overwrote direct state')
    except urllib.error.HTTPError as e:assert e.code==400
    st=wait(lambda s:s['display']=='off',45);frames=st['renderedFrames'];time.sleep(3);assert status()['renderedFrames']==frames
    fixture['remaining']=60;refresh();st=wait(lambda s:s['remaining']==60 and s['display']=='on');assert st['idleSeconds']<3
    print('PASS validation is atomic, old bridge rejected, USB auto-off stops drawing, new data wakes',flush=True)
finally:
    restore={k:v for k,v in old['config'].items() if k!='hasApiKey'};restore['clearApiKey']=True
    save(restore,screen=old['status']['screen'],viscosity=old['status']['viscosity'])
    server.shutdown();server.server_close()
