import { readFile, writeFile, mkdir, rename } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { createInterface } from 'node:readline';
import { resolve } from 'node:path';
import { overviewMetrics, overviewProviders, compact as compactRaw } from './src/usage.ts';
import { traceToScreen, routeEvents } from './src/types.ts';
const compact = value => compactRaw(value).replace('—', '--');
export const defaults = { provider: 'codex', accountId: '', windowId: '', brightness: 128, viscosity: .45, screen: 1, background: false, deviceHost: '' };
export function validateConfig(value) {
  const c = { ...defaults, ...value };
  for (const k of Object.keys(c)) if (!(k in defaults)) throw Error('未知配置项');
  for (const k of ['provider', 'accountId', 'windowId']) if (typeof c[k] !== 'string' || c[k].length > 160) throw Error('无效选择');
  if (!Number.isInteger(c.brightness) || c.brightness < 10 || c.brightness > 255) throw Error('亮度应为 10–255');
  if (!Number.isFinite(c.viscosity) || c.viscosity < 0 || c.viscosity > 1) throw Error('黏性应为 0–1');
  if (![1, 2, 3].includes(c.screen) || typeof c.background !== 'boolean') throw Error('无效屏幕配置');
  if (typeof c.deviceHost !== 'string' || c.deviceHost && !/^(magpie-screen\.local|192\.168\.\d{1,3}\.\d{1,3}|10\.\d{1,3}\.\d{1,3}\.\d{1,3}|172\.(1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3})$/.test(c.deviceHost)) throw Error('请输入局域网 IPv4 或 magpie-screen.local');
  return c;
}
export function quotaSelection(accounts, config) {
  const candidates = accounts.filter(a => a.provider === config.provider);
  const account = config.accountId ? candidates.find(a => a.id === config.accountId) : candidates[0];
  const window = config.windowId ? account?.windows.find(w => w.id === config.windowId) : account?.windows[0];
  return { provider: account?.provider || config.provider, remaining: window?.remaining ?? null, window: window?.name || '', readAt: account?.readAt || null };
}
export async function createDevice(root) {
  const dir = resolve(root, '.local');
  await mkdir(dir, { recursive: true, mode: 0o700 });
  const path = resolve(dir, 'device-config.json');
  let stored;
  try { stored = JSON.parse(await readFile(path, 'utf8')); } catch (e) { if (e.code !== 'ENOENT') throw e; }
  let config = validateConfig(stored?.config || defaults);
  const token = stored?.token || randomBytes(24).toString('hex');
  async function persist() { await writeFile(path + '.tmp', JSON.stringify({ config, token }), { mode: 0o600 }); await rename(path + '.tmp', path); }
  await persist();
  let child, status = {}, statusAt = 0, transport = '', seq = 0, stopped = false, ota = false, lastError = '';
  let accounts = [], quota = { provider: config.provider, remaining: null }, usage = null, route = null;
  let routes = new Map(), baseline = false, pid = 0, traceSeq = 0;
  const pending = new Map(), timers = new Set();
  const later = (fn, ms) => { if (stopped) return; const t = setTimeout(() => { timers.delete(t); if (!stopped) fn(); }, ms); timers.add(t); };
  async function bridge() {
    const python = process.env.MAGPIE_PYTHON || 'python3';
    child = spawn(python, [resolve(root, 'scripts/serial_bridge.py')], { stdio: ['pipe', 'pipe', 'pipe'] });
    child.stderr.on('data', () => { lastError = 'USB 桥接不可用，请安装 pyserial 或设置 MAGPIE_PYTHON'; });
    child.stdin.on('error', () => {});
    createInterface({ input: child.stdout }).on('line', line => {
      try {
        const m = JSON.parse(line);
        if (m.type === 'status') { status = m; statusAt = Date.now(); transport = 'USB'; }
        if (m.id && pending.has(m.id)) { const p = pending.get(m.id); pending.delete(m.id); clearTimeout(p.timer); m.ok ? p.resolve(m) : p.reject(Error(m.error || '设备拒绝命令')); }
      } catch {}
    });
    child.on('error', () => { lastError = '无法启动 USB 桥接'; });
    child.on('exit', () => { if (!stopped) later(bridge, 3000); });
  }
  await bridge();
  function usb(message) {
    return new Promise((resolve, reject) => {
      const id = ++seq;
      const timer = setTimeout(() => { pending.delete(id); reject(Error('设备未确认 USB 命令')); }, 2500);
      pending.set(id, { resolve, reject, timer });
      child?.stdin.write(JSON.stringify({ ...message, id }) + '\n');
    });
  }
  const host = () => config.deviceHost || (status.ip && status.ip !== '0.0.0.0' ? status.ip : 'magpie-screen.local');
  async function wifi(message) {
    // Initial .local discovery can take about five seconds; later calls use the reported IP.
    const r = await fetch(`http://${host()}/command`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Magpie-Key': token }, body: JSON.stringify(message), signal: AbortSignal.timeout(8000), redirect: 'error' });
    if (!r.ok) throw Error('Wi-Fi 设备未确认配置');
    const m = await r.json();
    if (!m.ok) throw Error(m.error || '设备拒绝命令');
    if (m.status) { status = m.status; statusAt = Date.now(); transport = 'Wi-Fi'; }
    return m;
  }
  async function command(message) {
    if (ota) throw Error('OTA 更新中，请稍候');
    if (transport === 'USB' && Date.now() - statusAt < 5000) return usb(message);
    return wifi(message);
  }
  const snapshot = () => ({ quota, usage, route });
  const sendState = async () => { try { await command({ op: 'state', state: snapshot() }); lastError = ''; } catch (e) { lastError = e.message; } };
  async function poll(path) { const r = await fetch('http://127.0.0.1:4173' + path, { signal: AbortSignal.timeout(28000) }); if (!r.ok) throw Error('采集失败'); return r.json(); }
  async function quotas() {
    try { accounts = (await poll('/api/quotas')).accounts; quota = quotaSelection(accounts, config); } catch { accounts = []; quota = { provider: config.provider, remaining: null }; }
    await sendState(); later(quotas, 30000);
  }
  async function overview() {
    try {
      const data = await poll('/api/usage'), m = overviewMetrics(data), pct = n => n == null ? '--' : (n * 100).toFixed(1) + '%';
      usage = { total: compact(m.totalTokens), input: compact(data.input), output: compact(data.output), cache: compact(m.cache), calls: compact(data.calls), success: pct(m.successRate), cost: data.cost == null ? '--' : '$' + (data.cost >= 1000 ? compact(data.cost) : data.cost.toFixed(2)), unpriced: data.unpriced, cacheRate: pct(m.cacheRate), speed: m.speed == null ? '--' : (m.speedIsFloor ? '>=' : '') + (m.speed >= 1000 ? compact(m.speed) : Math.round(m.speed)), providers: overviewProviders(data.providers).map(p => ({ name: p.key === 'other' ? 'OTHER' : p.key.replace('provider:', ''), tokens: compact(p.tokens), fraction: data.tokens ? (p.tokens || 0) / data.tokens : 0 })) };
    } catch { usage = null; }
    await sendState(); later(overview, 15000);
  }
  async function trace() {
    try {
      const j = await poll(`/api/trace?after=${traceSeq}&wait=1&background=${config.background ? 1 : 0}`);
      if (pid !== j.pid || j.seq < traceSeq) { routes.clear(); baseline = false; }
      pid = j.pid; traceSeq = j.seq;
      const before = new Map(routes);
      const ids = new Set(j.ids); for (const id of routes.keys()) if (!ids.has(id)) routes.delete(id);
      for (const r of j.routes) routes.set(r.id, r);
      const list = [...routes.values()], s = traceToScreen(list);
      const events = baseline ? routeEvents(before, list, false) : [];
      baseline = true;
      route = { state: s.state, sources: s.sources, targets: (s.targets || []).slice(0, 4).map(t => ({ key: t.key, provider: t.provider, model: t.model, active: t.active, failed: t.failed })), events: events.slice(-12) };
    } catch { route = null; baseline = false; }
    await sendState(); if (route) route.events = []; later(trace, route ? 100 : 3000);
  }
  // Pair only over the physical USB link. The Wi-Fi key is never returned by the GUI API.
  async function heartbeat() {
    try {
      if (transport === 'USB' && Date.now() - statusAt < 5000 && !status.paired) {
        await usb({ op: 'pair', token });
        await usb({ op: 'config', config });
        await sendState();
      } else if (!ota) await command({ op: 'ping' });
    } catch (e) { lastError = e.message; }
    later(heartbeat, 2000);
  }
  later(quotas, 500); later(overview, 800); later(trace, 1000); later(heartbeat, 1500);
  async function body(req, max = 16384) { let size = 0; const chunks = []; for await (const c of req) { size += c.length; if (size > max) throw Error('请求过大'); chunks.push(c); } return Buffer.concat(chunks); }
  let writing = false;
  async function handler(req, res) {
    const path = new URL(req.url, 'http://127.0.0.1').pathname;
    if (req.method === 'GET' && (path === '/api/device/open-config' || path === '/config')) {
      res.writeHead(302, { Location: `http://${host()}/config`, 'Cache-Control': 'no-store' });
      res.end(); return true;
    }
    if (!path.startsWith('/api/device')) return false;
    const send = (code, v) => { res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); res.end(JSON.stringify(v)); };
    if (!['127.0.0.1:4173', 'localhost:4173'].includes(req.headers.host) || req.headers.origin && !['http://127.0.0.1:4173', 'http://localhost:4173'].includes(req.headers.origin)) { send(403, { error: '来源不允许' }); return true; }
    try {
      if (req.method === 'GET' && path === '/api/device') send(200, { config, accounts, status, connected: Date.now() - statusAt < 6000, transport, lastError, ota });
      else if (req.method === 'POST') {
        if (writing) { send(409, { error: '上一项设备操作仍在进行' }); return true; }
        writing = true;
        try {
          if (path === '/api/device/config') {
            const next = validateConfig(JSON.parse((await body(req)).toString()));
            const previous = config;
            config = { ...config, deviceHost: next.deviceHost };
            let ack;
            try { ack = await command({ op: 'config', config: next }); } catch (e) { config = previous; throw e; }
            config = next; await persist(); quota = quotaSelection(accounts, config); await sendState();
            send(200, { ok: true, config, ack });
          } else if (path === '/api/device/wifi') {
            const v = JSON.parse((await body(req)).toString());
            if (typeof v.ssid !== 'string' || !v.ssid || Buffer.byteLength(v.ssid) > 32 || typeof v.password !== 'string' || Buffer.byteLength(v.password) > 63 || v.password.length > 0 && Buffer.byteLength(v.password) < 8) throw Error('SSID 或密码长度无效');
            await command({ op: 'wifi', ssid: v.ssid, password: v.password });
            send(200, { ok: true });
          } else if (path === '/api/device/ota') {
            const binary = await body(req, 6500000);
            if (binary.length < 1024 || binary[0] !== 0xe9) throw Error('请选择 ESP32 应用 firmware.bin');
            if (!status.ip || status.ip === '0.0.0.0') throw Error('请先完成 Wi-Fi 配网');
            ota = true;
            try {
              const form = new FormData(); form.set('firmware', new Blob([binary]), 'firmware.bin');
              const r = await fetch(`http://${host()}/update`, { method: 'POST', headers: { 'X-Magpie-Key': token }, body: form, signal: AbortSignal.timeout(120000), redirect: 'error' });
              const result = await r.json(); if (!r.ok || !result.ok) throw Error(result.error || 'OTA 写入失败');
              send(200, { ok: true, message: '固件写入完成，设备正在重启' });
            } finally { ota = false; }
          } else send(404, { error: '接口不存在' });
        } finally { writing = false; }
      } else send(405, { error: '方法不支持' });
    } catch (e) { send(400, { error: e.message }); }
    return true;
  }
  return { handler, close() { stopped = true; for (const t of timers) clearTimeout(t); for (const p of pending.values()) { clearTimeout(p.timer); p.reject(Error('服务关闭')); } pending.clear(); child?.kill(); } };
}
