<script setup lang="ts">
import { computed, onUnmounted, ref, watch } from "vue";
import RouteScreen from "./RouteScreen.vue";
import { useQuotas } from "./useQuotas";
import { useDeviceMotion } from "./useDeviceMotion";
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
import {
  demoUsage,
  emptyUsage,
  compact,
  providerName,
  type UsageData,
} from "./usage";
interface Session {
  token: string;
  label: string;
  model: string;
  time: string;
}
const mode = ref("sim"),
  view = ref("liquid"),
  usage = ref<UsageData | null>(null),
  usageStatus = ref("idle"),
  usageError = ref(""),
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
const rotation = ref(0), autoUpright = ref(true), remaining = ref(80), viscosity = ref(.65), liquidProvider = ref("codex");
const quotaMode = ref("live");
const quota = useQuotas(computed(() => view.value === "liquid" && quotaMode.value === "live"));
const displayRemaining = computed(() => quotaMode.value === "live" ? quota.remaining.value : remaining.value);
const displayProvider = computed(() => quotaMode.value === "live" ? quota.account.value?.provider || "" : liquidProvider.value);
const quotaCaption = computed(() => {
  if (quotaMode.value === "sim") return "剩余额度 / REMAINING";
  if (quota.status.value === "loading") return "正在读取额度…";
  if (quota.status.value === "offline") return "额度数据不可用";
  if (!quota.accounts.value.length) return "暂无额度账号";
  if (quota.account.value?.unavailable) return "账号额度暂不可用";
  if (!quota.window.value) return "暂无额度窗口";
  return quota.remaining.value == null ? `${quota.window.value.name} · 余量未知` : `剩余额度 · ${quota.window.value.name}`;
});
const quotaTime = (value: string | null | undefined) => value ? new Date(value).toLocaleString() : "—";
const { angle: deviceAngle, upright, moving } = useDeviceMotion(rotation, autoUpright);
let shakeTimer: ReturnType<typeof setTimeout> | undefined;
let shakeGeneration = 0;
function stopShake() { clearTimeout(shakeTimer); shakeGeneration++; }
function turn(delta: number) { stopShake(); rotation.value = Math.max(-360, Math.min(360, rotation.value + delta)); }
let dragStart: { x: number; angle: number } | null = null;
function beginRotate(event: PointerEvent) {
  stopShake();
  dragStart = { x: event.clientX, angle: rotation.value };
  (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
}
function dragRotate(event: PointerEvent) {
  if (dragStart) rotation.value = Math.max(-360, Math.min(360, dragStart.angle + (event.clientX - dragStart.x) * .7));
}
function shake() {
  clearTimeout(shakeTimer);
  const own = ++shakeGeneration;
  const base = rotation.value;
  const offsets = [35, -30, 21, -13, 0];
  function next() {
    if (own !== shakeGeneration) return;
    const offset = offsets.shift();
    if (offset == null) return;
    rotation.value = Math.max(-360, Math.min(360, base + offset));
    shakeTimer = setTimeout(next, 180);
  }
  next();
}
onUnmounted(() => clearTimeout(shakeTimer));
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
const usageData = computed(() =>
  mode.value === "sim"
    ? sim.value === "offline"
      ? null
      : ["empty", "idle"].includes(sim.value)
        ? emptyUsage
        : demoUsage
    : usage.value,
);
const displayedUsageStatus = computed(() =>
  mode.value === "sim"
    ? sim.value === "offline"
      ? "offline"
      : "ready"
    : usageStatus.value,
);
let usageController: AbortController | undefined,
  usageGeneration = 0;
async function connectUsage() {
  usageController?.abort();
  const own = ++usageGeneration;
  usage.value = null;
  usageError.value = "";
  if (
    mode.value !== "live" ||
    view.value !== "overview" ||
    !running.value
  ) {
    usageStatus.value = running.value ? "idle" : "paused";
    return;
  }
  const c = new AbortController();
  usageController = c;
  usageStatus.value = "loading";
  while (!c.signal.aborted && own === usageGeneration) {
    try {
      const j = await json("/api/usage", c.signal);
      if (own !== usageGeneration) return;
      usage.value = j;
      usageStatus.value = "ready";
      usageError.value = "";
    } catch (e) {
      if (c.signal.aborted || own !== usageGeneration) return;
      usage.value = null;
      usageStatus.value = "offline";
      usageError.value = e instanceof Error ? e.message : "用量不可用";
    }
    await new Promise<void>((resolve) => {
      const end = () => {
        clearTimeout(timer);
        c.signal.removeEventListener("abort", end);
        resolve();
      };
      const timer = setTimeout(
        end,
        usageStatus.value === "offline" ? 3000 : 15000,
      );
      c.signal.addEventListener("abort", end, { once: true });
    });
  }
}
watch([mode, view, running], connectUsage);
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
  if (mode.value !== "live" || view.value !== "route" || !running.value) {
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
  [
    mode,
    view,
    selected,
    manualActive,
    background,
    running,
    identity,
    allSessions,
  ],
  connect,
);
onUnmounted(() => {
  generation++;
  controller?.abort();
  usageGeneration++;
  usageController?.abort();
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
  if (view.value !== "route") connectUsage();
  else connect();
}
</script>
<template>
  <main>
    <header>
      <a class="brand" href="/"><img :src="magpieIcon" alt="Magpie" /></a>
      <div>
        <h1>magpie.screen<span class="terminal-cursor" aria-hidden="true">_</span></h1>
        <p>你的桌面，一块小小的屏幕。 / LOCAL SCREEN STUDIO</p>
      </div>
      <span class="local-badge">LOCAL PREVIEW <i /> </span>
    </header>
    <section class="workspace">
      <div class="preview-area">
        <div class="preview-label">
          <span>01 / SCREEN PREVIEW</span
          ><span>方形 AMOLED · 480 × 480 设计画布</span>
        </div>
        <div class="terminal-window">
          <div class="terminal-titlebar" aria-hidden="true">
            <i class="window-dot" /><i class="window-dot" /><i class="window-dot" />
            <code>magpie@local:~/{{ view }}</code><span>480 × 480</span>
          </div>
          <div class="device-space">
            <div
              class="device rotatable-device"
              @pointerdown="beginRotate"
              @pointermove="dragRotate"
              @pointerup="dragStart = null"
              @pointercancel="dragStart = null"
              @lostpointercapture="dragStart = null"
              :style="{
                transform: `scale(calc(var(--preview-fit, 1) * ${scale / 100}))`,
                rotate: `${deviceAngle}deg`,
                '--speed': speed + 's',
              }"
            >
              <RouteScreen
                :data="data"
                :angle="deviceAngle"
                :upright="autoUpright ? upright : 0"
                :remaining="displayRemaining"
                :quota-caption="quotaCaption"
                :viscosity="viscosity"
                :provider="displayProvider"
                :usage="usageData"
                :usage-status="displayedUsageStatus"
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
        </div>
        <div class="rotation-preview-hint">↔ 拖动屏幕旋转 <span>{{ Math.round(deviceAngle) }}°</span></div>
        <div class="view-switch">
          <button :class="{ active: view === 'liquid' }" @click="view = 'liquid'">01 液体余量</button>
          <button :class="{ active: view === 'route' }" @click="view = 'route'">02 路由</button>
          <button :class="{ active: view === 'overview' }" @click="view = 'overview'">03 今日概览</button>
        </div>
      </div>
      <aside>
        <div class="panel-title">
          <span>控制台</span><small>SCREEN STUDIO</small>
        </div>
          <div class="motion-controls">
            <label class="field-label" for="rotation">设备旋转 <span>{{ Math.round(deviceAngle) }}°</span></label>
            <input id="rotation" type="range" min="-360" max="360" step="1" v-model.number="rotation" @pointerdown="stopShake" @keydown="stopShake" />
            <div class="rotation-actions">
              <button @click="turn(-90)">↶ 90°</button><button @click="turn(90)">↷ 90°</button>
              <button @click="turn(180)">倒置</button><button @click="turn(-rotation)">归零</button>
            </div>
            <div class="motion-state"><span :class="{ moving }">{{ moving ? '旋转中 · 内容随屏幕' : view === 'liquid' ? '已停稳 · 液体自然回落' : '已停稳' }}</span></div>
            <label class="check"><input type="checkbox" v-model="autoUpright" />停止后翻正内容</label>
          </div>
        <template v-if="view === 'liquid'">
          <label class="field-label">额度数据源</label>
          <div class="segmented"><button :class="{ active: quotaMode === 'live' }" @click="quotaMode = 'live'">真实 Magpie</button><button :class="{ active: quotaMode === 'sim' }" @click="quotaMode = 'sim'">模拟预览</button></div>
          <template v-if="quotaMode === 'live'">
            <div class="screen-readout quota-readout"><span>{{ quota.status.value === 'loading' ? '正在读取…' : quota.status.value === 'offline' ? '额度读取失败' : '真实额度 · 每 30 秒读取' }}</span><p>{{ quota.error.value || quotaCaption }}</p></div>
            <label class="field-label" for="quota-account">账号</label>
            <select id="quota-account" v-model="quota.accountId.value" :disabled="!quota.accounts.value.length">
              <option v-if="!quota.accounts.value.length" value="">暂无账号</option>
              <option v-for="a in quota.accounts.value" :key="a.id" :value="a.id">{{ a.label }}{{ a.plan ? ' · ' + a.plan : '' }}</option>
            </select>
            <label class="field-label" for="quota-window">额度窗口</label>
            <select id="quota-window" v-model="quota.windowId.value" :disabled="!quota.account.value?.windows.length">
              <option v-if="!quota.account.value?.windows.length" value="">暂无窗口</option>
              <option v-for="w in quota.account.value?.windows || []" :key="w.id" :value="w.id">{{ w.name }} · {{ w.remaining == null ? '未知' : w.remaining + '% 剩余' }}</option>
            </select>
            <p class="hint">Magpie 采集：{{ quotaTime(quota.account.value?.readAt) }}<br />窗口重置：{{ quotaTime(quota.window.value?.resetsAt) }}<br />网页读取：{{ quotaTime(quota.fetchedAt.value) }}</p>
            <button class="demo-play" @click="quota.refresh">刷新额度</button>
          </template>
          <template v-else>
            <div class="notice">模拟预览 · 手动调整余量</div>
            <label class="field-label">供应商</label>
            <div class="segmented"><button :class="{ active: liquidProvider === 'codex' }" @click="liquidProvider = 'codex'">Codex / 黑白</button><button :class="{ active: liquidProvider === 'claude' }" @click="liquidProvider = 'claude'">Claude / 橙色</button></div>
            <label class="field-label" for="remaining">剩余额度 <span>{{ remaining }}%</span></label>
            <input id="remaining" type="range" min="0" max="100" v-model.number="remaining" />
          </template>
          <label class="field-label" for="viscosity">液体黏性 <span>{{ Math.round(viscosity * 100) }}%</span></label>
          <input id="viscosity" type="range" min="0" max="1" step=".01" v-model.number="viscosity" />
          <div class="viscosity-labels"><span>轻盈 · 回荡</span><span>厚重 · 缓流</span></div>
          <button class="demo-play" @click="shake">轻晃一次</button>
          <p class="panel-note">整个屏幕是容器。液量保持不变，旋转只改变液体分布。{{ quotaMode === 'sim' ? '当前余量为模拟值。' : '液位对应所选账号、窗口的剩余百分比。' }}</p>
        </template>
        <template v-else>
        <div class="screen-readout" v-if="view !== 'route'">
          <span>{{ mode === "sim" ? "模拟" : "真实" }} · 今日全部来源</span>
          <p>{{ usageError || "用量每 15 秒刷新，与路由会话筛选独立。" }}</p>
          <p v-if="usageData?.at">
            数据读取：{{ new Date(usageData.at).toLocaleTimeString() }}
          </p>
        </div>
        <div class="screen-readout" v-else>
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
              {{ view !== "route" && s === "empty" ? "暂无用量" : labels[s] }}
            </button>
          </div>
          <button v-if="view === 'route'" class="demo-play" @click="demoRun++">
            播放一次模拟事件
          </button>
          <template v-if="view === 'route'">
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
        </template>
        <template v-else-if="view === 'route'"
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
        <div
          v-if="view === 'route' && mode === 'live' && data.targets?.length"
          class="route-debug"
        >
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
        <template v-if="view !== 'route' && mode === 'live'"
          ><div class="actions usage-actions">
            <button @click="reconnect">重新读取</button
            ><button @click="running = false">暂停</button>
          </div>
          <div
            class="usage-debug"
            v-for="p in usageData?.providers || []"
            :key="p.id"
          >
            <b>{{ providerName(p.id) }}</b
            ><span
              >{{ compact(p.calls) }} 次 · {{ compact(p.tokens) }} tokens</span
            >
          </div></template
        >
        <div v-if="view === 'overview'" class="panel-note">
          失败请求：{{ usageData?.errors ?? '—' }} 次<br />
          缓存命中率：缓存读取占提示词总量的比例<br />
          重试后成功：{{ usageData?.retried ?? '—' }} 次<br />
          首字计时样本：{{ usageData?.timed ?? '—' }} 次<br />
          输出计时样本：{{ usageData?.decodeN ?? '—' }} 次<br />
          平均首字：{{ usageData?.timed && usageData?.ttftMs != null ? (usageData.ttftMs / usageData.timed / 1000).toFixed(2) + ' 秒' : '—' }}
        </div>
        </template>
        <div class="divider" />
        <label class="field-label" for="scale"
          >预览缩放 <span>{{ scale }}%</span></label
        ><input
          id="scale"
          type="range"
          min="65"
          max="100"
          v-model.number="scale"
        /><template v-if="view === 'route'"
          ><label class="field-label" for="speed"
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
        </template>
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
