<script setup lang="ts">
import { computed, onUnmounted, ref, watch } from "vue";
import RouteScreen from "./RouteScreen.vue";
import magpieIcon from "./assets/magpie.svg";
import chatgptIcon from "./assets/chatgpt.svg";
import claudeIcon from "./assets/claude.svg";
import cursorIcon from "./assets/cursor.svg";
import {
  labels,
  traceToScreen,
  routeEvents,
  type RouteEvent,
  type TraceRoute,
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
  identity = ref(false),
  allSessions = ref(false),
  demoRequests = ref(2),
  demoBranches = ref(2),
  traces = ref<TraceRoute[]>([]),
  events = ref<RouteEvent[]>([]),
  demoRun = ref(0),
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
  events:
    demoRun.value && ["pending", "success", "failure"].includes(sim.value)
      ? Array.from({ length: demoRequests.value }, (_, i) => [
          {
            key: `demo-${demoRun.value}-${i}:request`,
            kind: "request" as const,
            target: "",
            failed: false,
          },
          {
            key: `demo-${demoRun.value}-${i}:attempt`,
            kind: "attempt" as const,
            target: `demo-${i % demoBranches.value}`,
            failed: false,
          },
          ...(sim.value !== "pending"
            ? [
                {
                  key: `demo-${demoRun.value}-${i}:response`,
                  kind: "response" as const,
                  target: `demo-${i % demoBranches.value}`,
                  failed: sim.value === "failure",
                },
                {
                  key: `demo-${demoRun.value}-${i}:result`,
                  kind: "result" as const,
                  target: "",
                  failed: sim.value === "failure",
                },
              ]
            : []),
        ]).flat()
      : [],
  targets: ["pending", "success", "failure"].includes(sim.value)
    ? Array.from({ length: demoBranches.value }, (_, i) => ({
        key: `demo-${i}`,
        account: `模拟账号 ${String(i + 1).padStart(2, "0")}`,
        provider: "codex",
        model: i % 2 ? "gpt-5.6-luna" : "gpt-6-astra",
        effort: i % 2 ? "low" : "high",
        done: sim.value !== "pending",
        status:
          sim.value === "failure" ? 500 : sim.value === "success" ? 200 : 0,
        failed: sim.value === "failure",
        index: i,
        active:
          sim.value === "pending"
            ? Array.from(
                { length: demoRequests.value },
                (_, n) => n % demoBranches.value,
              ).filter((n) => n === i).length
            : 0,
        requests: 1,
      }))
    : [],
  flights:
    sim.value === "pending"
      ? Array.from({ length: demoRequests.value }, (_, i) => ({
          key: `demo-request-${i}`,
          target: `demo-${i % demoBranches.value}`,
          agent: "codex",
        }))
      : [],
  sources: ["codex"],
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
        : !allSessions.value && !selected.value && !manualActive.value
          ? {
              ...toScreen(null, false),
              state: "idle" as ScreenState,
              detail: "请选择会话或等待新记录",
            }
          : { ...traceToScreen(traces.value), events: events.value },
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
  traces.value = [];
  events.value = [];
  error.value = "";
  at.value = "";
  if (mode.value !== "live" || !running.value) {
    connection.value = "未连接";
    return;
  }
  const c = new AbortController();
  controller = c;
  connection.value = "连接中";
  let tracePid = 0;
  const knownRoutes = new Map<number, TraceRoute>();
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
        if (
          !allSessions.value &&
          !selected.value &&
          !manualActive.value &&
          j.sessions.length
        ) {
          selected.value = j.sessions[0].token;
          return;
        }
      }
      if (allSessions.value || selected.value || manualActive.value) {
        const q = new URLSearchParams({
          after: String(seq),
          wait: firstRoute ? "0" : "1",
          identity: identity.value ? "full" : "anonymous",
          background: background.value ? "1" : "0",
        });
        if (!allSessions.value) {
          if (manualActive.value) q.set("manual", manualActive.value);
          else q.set("session", selected.value);
        }
        const j = await json("/api/trace?" + q, c.signal);
        if (own !== generation) return;
        const reset = j.seq < seq || !!(tracePid && tracePid !== j.pid);
        if (reset) {
          knownRoutes.clear();
          events.value = [];
        }
        const changes = routeEvents(knownRoutes, j.routes, firstRoute || reset);
        if (changes.length) events.value = changes;
        tracePid = j.pid;
        seq = j.seq;
        firstRoute = false;
        const ids = new Set<number>(j.ids);
        for (const id of knownRoutes.keys())
          if (!ids.has(id)) knownRoutes.delete(id);
        for (const r of j.routes) knownRoutes.set(r.id, r);
        traces.value = [...knownRoutes.values()]
          .sort((a, b) => b.id - a.id)
          .slice(0, 60);
        route.value = traces.value[0] || null;
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
      traces.value = [];
      events.value = [];
      knownRoutes.clear();
      seq = 0;
      tracePid = 0;
      firstRoute = true;
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
watch(
  [mode, selected, manualActive, background, running, identity, allSessions],
  connect,
);
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
              :speed="speed"
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
        <div class="screen-readout">
          <span
            >{{ mode === "sim" ? "模拟" : "真实" }} ·
            {{ labels[data.state] }}</span
          >
          <p>{{ data.detail }}</p>
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
          </div>
          <button class="demo-play" @click="demoRun++">播放一次模拟事件</button>
          <label class="field-label" for="demo-requests"
            >模拟并发请求 <span>{{ demoRequests }}</span></label
          >
          <input
            id="demo-requests"
            type="range"
            min="1"
            max="4"
            v-model.number="demoRequests"
          />
          <label class="field-label" for="demo-branches"
            >模拟目的地 <span>{{ demoBranches }}</span></label
          >
          <input
            id="demo-branches"
            type="range"
            min="1"
            max="4"
            v-model.number="demoBranches"
          />
        </template>
        <template v-else
          ><div class="connection" :class="{ good: connection === '已连接' }">
            <i />{{ connection }}<span>{{ running ? "只读" : "已暂停" }}</span>
          </div>
          <label class="check"
            ><input
              type="checkbox"
              v-model="identity"
            />显示完整账号（仅本机内存）</label
          ><label class="check"
            ><input
              type="checkbox"
              v-model="allSessions"
            />查看所有会话的活动请求</label
          ><label class="field-label" for="session">会话发现</label
          ><select
            id="session"
            v-model="selected"
            @change="choose"
            :disabled="allSessions"
          >
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
            }}<br />{{
              error || "状态按真实 trace 汇总，可显示并发与已尝试路由。"
            }}
          </p></template
        >
        <div v-if="mode === 'live' && data.targets?.length" class="route-debug">
          <label class="field-label"
            >实际目的地 · {{ data.targets.length }}</label
          >
          <div v-for="t in data.targets" :key="t.key">
            <b>{{ t.account }}</b
            ><code>{{ t.provider }}/{{ t.model }}</code
            ><small
              >{{ t.active }} 个尝试进行中 · {{ t.effort || "默认强度" }}</small
            >
          </div>
          <p class="hint">
            屏幕最多显示 4 个目的地、8 组飞行动画；其余记录在此列出。
          </p>
        </div>
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
          >单次飞行 <span>{{ (speed * 0.4).toFixed(1) }}s</span></label
        ><input
          id="speed"
          type="range"
          min="1"
          max="5"
          step="0.1"
          v-model.number="speed"
        />
        <div class="panel-note">
          仅在新请求、开始尝试、首个响应或结束事件出现时飞行一次。<br />等待期间静止；首次连接只同步状态，不重放历史。
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
      <span>MAGPIE SCREEN / 0.2 · 事件动画</span
      ><span>Web 预览工作台 · ESP32-S3 方形屏 · 固件适配待后续完成</span>
    </footer>
  </main>
</template>
