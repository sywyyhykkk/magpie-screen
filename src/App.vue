<script setup lang="ts">
import { computed, onUnmounted, ref, watch } from "vue";
import RouteScreen from "./RouteScreen.vue";
import magpieIcon from "./assets/magpie.svg";
import chatgptIcon from "./assets/chatgpt.svg";
import claudeIcon from "./assets/claude.svg";
import cursorIcon from "./assets/cursor.svg";
import {
  labels,
  toScreen,
  type Route,
  type ScreenState,
  type ScreenData,
} from "./types";
interface Session {
  token: string;
  label: string;
  model: string;
  time: string;
}
const mode = ref("sim"),
  view = ref("route"),
  sim = ref<ScreenState>("pending"),
  selected = ref(""),
  manual = ref(""),
  manualActive = ref(""),
  background = ref(false),
  running = ref(true),
  sessions = ref<Session[]>([]),
  route = ref<Route | null>(null),
  connection = ref("未连接"),
  at = ref(""),
  error = ref(""),
  scale = ref(100),
  speed = ref(2.4);
let controller: AbortController | undefined,
  generation = 0;
const fake = computed<ScreenData>(() => ({
  state: sim.value,
  model: ["idle", "empty", "offline"].includes(sim.value)
    ? "—"
    : "codex/gpt-6-astra",
  effort: "high",
  account: "模拟账号 01",
  detail:
    sim.value === "success"
      ? "HTTP 200 · 已结束"
      : sim.value === "failure"
        ? "HTTP 500 · 已结束"
        : "演示数据 · 不发送模型请求",
  tries: ["pending", "success", "failure"].includes(sim.value) ? 1 : 0,
}));
const data = computed(() =>
  mode.value === "sim"
    ? fake.value
    : connection.value === "连接失败"
      ? {
          ...toScreen(null, false),
          state: "offline" as ScreenState,
          detail: error.value,
        }
      : !running.value
        ? {
            ...toScreen(null, false),
            state: "idle" as ScreenState,
            detail: "连接已暂停",
          }
        : !selected.value && !manualActive.value
          ? {
              ...toScreen(null, false),
              state: "idle" as ScreenState,
              detail: "请选择会话或等待新记录",
            }
          : toScreen(route.value, background.value),
);
async function json(path: string, signal: AbortSignal) {
  const r = await fetch(path, { signal, cache: "no-store" });
  const j = await r.json();
  if (!r.ok) throw Error(j.error || "连接失败");
  return j;
}
async function connect() {
  controller?.abort();
  const own = ++generation;
  route.value = null;
  error.value = "";
  at.value = "";
  if (mode.value !== "live" || !running.value) {
    connection.value = "未连接";
    return;
  }
  const c = new AbortController();
  controller = c;
  connection.value = "连接中";
  let seq = 0,
    lastDiscovery = 0;
  let firstRoute = true;
  while (!c.signal.aborted && own === generation) {
    try {
      if (Date.now() - lastDiscovery > 15000) {
        const j = await json(
          "/api/sessions?background=" + (background.value ? "1" : "0"),
          c.signal,
        );
        if (own !== generation) return;
        sessions.value = j.sessions;
        at.value = j.at;
        lastDiscovery = Date.now();
        if (!selected.value && !manualActive.value && j.sessions.length) {
          selected.value = j.sessions[0].token;
          return;
        }
      }
      if (selected.value || manualActive.value) {
        const q = new URLSearchParams({
          after: String(seq),
          wait: firstRoute ? "0" : "20",
        });
        if (manualActive.value) q.set("manual", manualActive.value);
        else q.set("session", selected.value);
        const j = await json("/api/route?" + q, c.signal);
        if (own !== generation) return;
        seq = j.seq;
        firstRoute = false;
        route.value = j.route;
        at.value = j.at;
      } else {
        const j = await json("/api/health", c.signal);
        if (own !== generation) return;
        at.value = j.at;
      }
      connection.value = "已连接";
      error.value = "";
    } catch (e) {
      if (c.signal.aborted || own !== generation) return;
      connection.value = "连接失败";
      route.value = null;
      error.value = e instanceof Error ? e.message : "连接失败";
    }
    await new Promise<void>((resolve) => {
      const end = () => {
        clearTimeout(t);
        c.signal.removeEventListener("abort", end);
        resolve();
      };
      const t = setTimeout(end, connection.value === "连接失败" ? 3000 : 700);
      c.signal.addEventListener("abort", end, { once: true });
    });
  }
}
watch([mode, selected, manualActive, background, running], connect);
onUnmounted(() => {
  generation++;
  controller?.abort();
});
function choose() {
  manualActive.value = "";
}
function applyManual() {
  selected.value = "";
  manualActive.value = manual.value.trim();
  connect();
}
function reconnect() {
  running.value = true;
  connect();
}
</script>
<template>
  <main>
    <header>
      <a class="brand" href="/"><img :src="magpieIcon" alt="Magpie" /></a>
      <div>
        <h1>Magpie Screen</h1>
        <p>你的桌面，一块小小的屏幕。</p>
      </div>
      <span class="local-badge">LOCAL PREVIEW <i /> </span>
    </header>
    <section class="workspace">
      <div class="preview-area">
        <div class="preview-label">
          <span>01 / SCREEN PREVIEW</span
          ><span>方形 AMOLED · 480 × 480 设计画布</span>
        </div>
        <div class="device-space">
          <div
            class="device"
            :style="{
              transform: `scale(${scale / 100})`,
              '--speed': speed + 's',
            }"
          >
            <RouteScreen
              :data="data"
              :mode="mode"
              :view="view"
              :connected="
                mode === 'sim'
                  ? sim === 'offline'
                    ? '连接失败'
                    : '模拟连接正常'
                  : connection
              "
            />
          </div>
        </div>
        <div class="below-preview">
          <span class="cross">＋</span> 方形屏幕预览 · 实机参数待核对
          <span class="cross">＋</span>
        </div>
        <div class="view-switch">
          <button :class="{ active: view === 'route' }" @click="view = 'route'">
            01 路由</button
          ><button
            :class="{ active: view === 'service' }"
            @click="view = 'service'"
          >
            02 服务状态
          </button>
        </div>
      </div>
      <aside>
        <div class="panel-title">
          <span>控制台</span><small>SCREEN STUDIO</small>
        </div>
        <label class="field-label">数据源</label>
        <div class="segmented">
          <button :class="{ active: mode === 'sim' }" @click="mode = 'sim'">
            模拟预览</button
          ><button :class="{ active: mode === 'live' }" @click="mode = 'live'">
            真实 Magpie
          </button>
        </div>
        <template v-if="mode === 'sim'"
          ><div class="notice">模拟模式 · 所有画面数据均为演示</div>
          <label class="field-label">场景</label>
          <div class="scenarios">
            <button
              v-for="s in [
                'idle',
                'pending',
                'success',
                'failure',
                'empty',
                'offline',
              ] as ScreenState[]"
              :key="s"
              :class="{ active: sim === s }"
              @click="sim = s"
            >
              {{ labels[s] }}
            </button>
          </div></template
        >
        <template v-else
          ><div class="connection" :class="{ good: connection === '已连接' }">
            <i />{{ connection }}<span>{{ running ? "只读" : "已暂停" }}</span>
          </div>
          <label class="field-label" for="session">会话发现</label
          ><select id="session" v-model="selected" @change="choose">
            <option value="">选择会话</option>
            <option v-for="s in sessions" :key="s.token" :value="s.token">
              {{ s.label }} · {{ s.model }}
            </option>
          </select>
          <p class="hint">今日最近 100 条 Codex 请求；不代表完整对话。</p>
          <label class="field-label" for="manual">手动会话 ID</label>
          <div class="input-row">
            <input
              id="manual"
              v-model="manual"
              maxlength="128"
              placeholder="仅在本机内存使用"
              @keydown.enter="applyManual"
            /><button @click="applyManual">连接</button>
          </div>
          <label class="check"
            ><input type="checkbox" v-model="background" />包含后台 /
            辅助请求</label
          >
          <div class="actions">
            <button @click="reconnect">重新连接</button
            ><button @click="running = false">暂停</button>
          </div>
          <p class="hint">
            数据读取：{{ at ? new Date(at).toLocaleTimeString() : "—" }}<br />
            请求时间：{{
              route?.time ? new Date(route.time).toLocaleString() : "—"
            }}<br />{{ error || "状态按会话最新请求显示。" }}
          </p></template
        >
        <div class="divider" />
        <label class="field-label" for="scale"
          >预览缩放 <span>{{ scale }}%</span></label
        ><input
          id="scale"
          type="range"
          min="65"
          max="100"
          v-model.number="scale"
        /><label class="field-label" for="speed"
          >动画周期 <span>{{ speed.toFixed(1) }}s</span></label
        ><input
          id="speed"
          type="range"
          min="1"
          max="5"
          step="0.1"
          v-model.number="speed"
        />
        <div class="panel-note">
          喜鹊往返为状态动画，不表示生成进度或实际包流向。<br />仅未完成状态循环飞行；结束状态收起动画。
        </div>
        <div class="asset-strip">
          <img :src="chatgptIcon" alt="ChatGPT" title="ChatGPT" /><img
            :src="claudeIcon"
            alt="Claude"
            title="Claude"
          /><img :src="cursorIcon" alt="Cursor" title="Cursor" /><span
            >已备素材 · 其他数据源待接入</span
          >
        </div>
      </aside>
    </section>
    <footer>
      <span>MAGPIE SCREEN / 0.1</span
      ><span>Web 预览工作台 · ESP32-S3 方形屏 · 固件适配待后续完成</span>
    </footer>
  </main>
</template>
