<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';
const config = ref({ provider: 'codex', accountId: '', windowId: '', brightness: 128, viscosity: .45, screen: 1, background: false, deviceHost: '' });
const device = ref<any>({ accounts: [], status: {} });
const loaded = ref(false), busy = ref(false), message = ref(''), error = ref(''), readError = ref('');
const ssid = ref(''), password = ref(''), file = ref<File>();
let timer: ReturnType<typeof setInterval>;
const providers = computed<string[]>(() => [...new Set<string>(['codex', 'claude', ...device.value.accounts.map((a: any) => a.provider)])]);
const accounts = computed(() => device.value.accounts.filter((a: any) => a.provider === config.value.provider));
const account = computed(() => accounts.value.find((a: any) => a.id === config.value.accountId) || (!config.value.accountId ? accounts.value[0] : null));
async function refresh() {
  try { const r = await fetch('/api/device'); if (!r.ok) throw Error('设备服务不可用'); device.value = await r.json(); readError.value = '';  if (!loaded.value) { config.value = device.value.config; loaded.value = true; } else if (!busy.value && device.value.connected) { config.value.screen = device.value.status.screen; } } catch (e: any) { readError.value = '本地服务暂不可用，正在重连'; device.value.connected = false; }
}
async function post(path: string, body: any, binary = false) {
  busy.value = true; message.value = ''; error.value = '';
  try {
    const r = await fetch('/api/device/' + path, { method: 'POST', headers: { 'Content-Type': binary ? 'application/octet-stream' : 'application/json' }, body: binary ? body : JSON.stringify(body) });
    const result = await r.json(); if (!r.ok) throw Error(result.error);
    message.value = path === 'wifi' ? 'Wi-Fi 凭据已送达，正在连接；下方 IP 出现后即可使用 OTA。' : path === 'ota' ? result.message : '开发板已确认，配置已实时生效并保存。';
    if (path === 'wifi') password.value = '';
    await refresh();
  } catch (e: any) { error.value = e.message; } finally { busy.value = false; }
}
function save() { return post('config', config.value); }
function providerChanged() { config.value.accountId = ''; config.value.windowId = ''; save(); }
onMounted(() => { refresh(); timer = setInterval(refresh, 2000); });
onUnmounted(() => clearInterval(timer));
</script>
<template>
  <main class="config-app">
    <header><a href="/">← 屏幕预览</a><span class="connection-pill" :class="{ online: device.connected }">{{ device.connected ? device.transport + ' 已连接' : '等待开发板' }}</span></header>
    <h1>设备配置<span>MAGPIE / SCREEN</span></h1>
    <p class="config-intro">选择你想看到的数据。修改后立即发送到开发板，收到设备确认才保存。</p>
    <div class="config-status" role="status">{{ busy ? '正在发送…' : error || readError || message || '通过 USB 连接开发板，即可开始配置。' }}</div>
    <div class="config-grid">
      <section><h2>01 / 液体余量</h2>
        <fieldset :disabled="busy || !loaded">
          <label>供应商<select v-model="config.provider" @change="providerChanged"><option v-for="p in providers" :key="p" :value="p">{{ p }}</option></select></label>
          <label>账号<select v-model="config.accountId" @change="config.windowId = ''; save()"><option value="">该供应商首个账号</option><option v-for="a in accounts" :key="a.id" :value="a.id">{{ a.label }}</option></select></label>
          <label>额度窗口<select v-model="config.windowId" @change="save"><option value="">首个额度窗口</option><option v-for="w in account?.windows || []" :key="w.id" :value="w.id">{{ w.name }} · {{ w.remaining == null ? '未知' : w.remaining + '%' }}</option></select></label>
          <p class="muted">指定账号或窗口不可用时显示未知，不自动切换到其他账号。</p>
          <label>液体黏性 <b>{{ config.viscosity.toFixed(2) }}</b><input type="range" v-model.number="config.viscosity" min="0" max="1" step=".05" @change="save" /></label>
        </fieldset>
      </section>
      <section><h2>02 / 显示与连接</h2><fieldset :disabled="busy || !loaded">
        <label>当前屏幕<select v-model.number="config.screen" @change="save"><option :value="1">1 · 液体余量</option><option :value="2">2 · 实时路由</option><option :value="3">3 · 今日概览</option></select></label>
        <label>屏幕亮度 <b>{{ Math.round(config.brightness / 255 * 100) }}%</b><input type="range" v-model.number="config.brightness" min="10" max="255" @change="save" /></label>
        <label class="check"><input type="checkbox" v-model="config.background" @change="save" />包含后台路由调用</label>
        <label>开发板地址<input v-model="config.deviceHost" placeholder="自动发现 / magpie-screen.local" @change="save" /></label>
        <dl><dt>设备 IP</dt><dd>{{ device.status.ip || '未连接 Wi-Fi' }}</dd><dt>当前屏幕 / 帧率</dt><dd>{{ device.status.screen || '—' }} / {{ device.status.fps?.toFixed(1) || '—' }} FPS</dd><dt>固件</dt><dd>{{ device.status.version || '等待设备' }}</dd><dt>Wi-Fi</dt><dd>{{ device.status.wifi || '等待设备' }}</dd></dl>
      </fieldset></section>
      <section><h2>03 / Wi-Fi 配网</h2><p class="muted">首次使用 USB 下发配置。支持 2.4 GHz Wi-Fi，重启后自动重连；密码仅保存到开发板。</p>
        <form @submit.prevent="post('wifi', { ssid, password })"><fieldset :disabled="busy || !device.connected"><label>网络名称 SSID<input v-model="ssid" required maxlength="32" autocomplete="off" /></label><label>Wi-Fi 密码<input type="password" v-model="password" maxlength="63" autocomplete="new-password" placeholder="开放网络留空" /></label><button type="submit">连接 Wi-Fi</button></fieldset></form>
      </section>
      <section><h2>04 / 无线更新</h2><p class="muted">配网成功后，选择构建生成的 firmware.bin。通过 Wi-Fi 写入备用应用分区，完成后自动重启。</p>
        <fieldset :disabled="busy || !device.connected"><label>应用固件<input type="file" accept=".bin" @change="file = ($event.target as HTMLInputElement).files?.[0]" /></label><button :disabled="!file || !device.status.ip || device.status.ip === '0.0.0.0'" @click="post('ota', file, true)">安装固件并重启</button></fieldset>
        <p class="muted">构建：<code>pio run -d firmware</code><br />文件：<code>firmware/.pio/build/liquid/firmware.bin</code></p>
      </section>
    </div>
  </main>
</template>
<style>
.config-app{max-width:1080px;margin:0 auto;padding:42px 28px 80px;color:#dfe8e1;font-family:ui-monospace,monospace}.config-app header{display:flex;justify-content:space-between;align-items:center}.config-app a{color:#a9b7aa}.config-app h1{font-size:32px;margin:38px 0 12px}.config-app h1 span{display:block;font-size:12px;letter-spacing:3px;color:#718478;margin-top:12px}.config-intro,.muted{font-size:13px;line-height:1.8;color:#8e9c92}.connection-pill{font-size:12px;border:1px solid #4a504b;border-radius:20px;padding:8px 12px}.connection-pill.online{color:#b8f3b2;border-color:#557452}.config-status{min-height:48px;padding:14px 0;color:#c9dc9f;font-size:13px}.config-grid{display:grid;grid-template-columns:1fr 1fr;gap:20px}.config-grid section{background:#141a17;border:1px solid #303b32;padding:26px;border-radius:12px}.config-grid h2{font-size:15px;margin:0 0 24px;color:#c1d9ae}.config-grid fieldset{border:0;padding:0;margin:0;min-width:0}.config-grid label{display:block;font-size:13px;margin:16px 0;color:#b9c7bc}.config-grid input:not([type=checkbox]),.config-grid select{display:block;width:100%;margin-top:9px;padding:11px;border:1px solid #39473d;background:#0c120f;color:#e1e9e2;border-radius:5px;box-sizing:border-box}.config-grid input[type=range]{padding:0;accent-color:#b1d9a0}.config-grid b{float:right}.config-grid button{background:#b6d99d;color:#172214;border:0;border-radius:5px;padding:12px 18px;cursor:pointer}.config-grid button:disabled{opacity:.45}.config-grid dl{display:grid;grid-template-columns:130px 1fr;font-size:12px;line-height:2}.config-grid dd{margin:0;color:#a7c89e}.config-grid dt{color:#839388}.config-grid code{overflow-wrap:anywhere;font-size:11px}@media(max-width:700px){.config-grid{grid-template-columns:1fr}.config-app{padding:24px 16px}}
</style>
