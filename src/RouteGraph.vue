<script setup lang="ts">
import { computed, ref, watch, onUnmounted } from "vue";
import type { ScreenData, ScreenTarget, RouteEvent } from "./types";
import FlyingMagpie from "./FlyingMagpie.vue";
import magpie from "./assets/magpie.svg";
import chatgpt from "./assets/chatgpt.svg";
import claude from "./assets/claude.svg";
import cursor from "./assets/cursor.svg";
const props = defineProps<{ data: ScreenData; speed?: number }>();
const targets = computed(() =>
  [...(props.data.targets || [])]
    .sort((a, b) => b.active - a.active)
    .slice(0, 4),
);
const height = computed(() => Math.max(100, targets.value.length * 54));
const mid = computed(() => height.value / 2);
const rowY = (index: number) =>
  targets.value.length === 1 ? mid.value : 27 + index * 54;
const sourceNames: Record<string, string> = {
  codex: "Codex",
  chatgpt: "ChatGPT",
  claude: "Claude",
  cursor: "Cursor",
};
const icons: Record<string, string> = {
  codex: chatgpt,
  chatgpt,
  claude,
  cursor,
};
const sources = computed(() =>
  props.data.sources?.length ? props.data.sources : ["codex"],
);
const iconFor = (p: string) => icons[p] || chatgpt;
interface PlayingEvent {
  event: RouteEvent;
  path: string;
  delay: number;
  duration: number;
  start: number;
}
const playing = ref<PlayingEvent[]>([]);
const timers = new Map<string, ReturnType<typeof setTimeout>>();
function clearFlights() {
  for (const timer of timers.values()) clearTimeout(timer);
  timers.clear();
  playing.value = [];
}
// Only newly observed event batches trigger animation. Snapshot refreshes do not.
watch(
  () => props.data.events,
  (batch) => {
    if (!batch?.length) {
      clearFlights();
      return;
    }
    const now = Date.now();
    const endByRequest = new Map<string, number>();
    for (const p of playing.value)
      endByRequest.set(
        p.event.key.split(":")[0],
        Math.max(
          endByRequest.get(p.event.key.split(":")[0]) || now,
          p.start + p.duration,
        ),
      );
    for (const event of batch.slice(0, 32)) {
      if (timers.has(event.key)) continue;
      if (
        (event.kind === "attempt" || event.kind === "response") &&
        !targets.value.some((t) => t.key === event.target)
      )
        continue;
      const request = event.key.split(":")[0];
      const start = Math.max(now, endByRequest.get(request) || now);
      const duration = Math.round((props.speed ?? 2.4) * 400);
      endByRequest.set(request, start + duration);
      let path =
        event.kind === "request"
          ? `M80 ${mid.value}H110`
          : event.kind === "result"
            ? `M110 ${mid.value}H80`
            : pathFor(event.target, event.kind === "response");
      playing.value.push({ event, path, delay: start - now, start, duration });
      timers.set(
        event.key,
        setTimeout(
          () => {
            playing.value = playing.value.filter(
              (p) => p.event.key !== event.key,
            );
            timers.delete(event.key);
          },
          start - now + duration + 50,
        ),
      );
    }
  },
);
onUnmounted(clearFlights);
const pathFor = (key: string, reverse = false) => {
  const index = targets.value.findIndex((t) => t.key === key);
  return reverse
    ? `M218 ${rowY(index)} C204 ${rowY(index)} 204 ${mid.value} 190 ${mid.value}`
    : `M190 ${mid.value} C204 ${mid.value} 204 ${rowY(index)} 218 ${rowY(index)}`;
};
const birdStyle = (p: PlayingEvent) => ({
  offsetPath: `path('${p.path}')`,
  animationDelay: `${p.delay}ms`,
  animationDuration: `${p.duration}ms`,
});
const state = (t: ScreenTarget) =>
  t.active
    ? t.responseSeen
      ? "已收到响应"
      : "等待响应"
    : t.failed
      ? "失败"
      : t.done && t.status >= 200 && t.status < 300
        ? "已结束"
        : "已尝试";
</script>
<template>
  <div class="route-graph trace-graph" :style="{ height: height + 'px' }">
    <svg class="route-wires" :viewBox="`0 0 400 ${height}`" aria-hidden="true">
      <path
        :d="`M80 ${mid}H110`"
        :class="{ live: (data.flights?.length || 0) > 0 }"
      />
      <path
        v-for="(t, i) in targets"
        :key="t.key"
        :d="pathFor(t.key)"
        :class="{ live: t.active }"
      />
      <path v-if="!targets.length" :d="`M190 ${mid}H218`" />
    </svg>
    <div class="route-node source" :style="{ top: mid - 43 + 'px' }">
      <div class="source-icons">
        <img v-for="s in sources" :key="s" :src="iconFor(s)" alt="" />
      </div>
      <b
        ><i />{{
          sources.length > 1
            ? "多客户端"
            : sourceNames[sources[0]] || sources[0]
        }}</b
      ><small>客户端</small>
    </div>
    <div class="route-node hub" :style="{ top: mid - 43 + 'px' }">
      <img :src="magpie" alt="" /><b>magpie</b><small>127.0.0.1</small>
    </div>
    <div
      v-for="(t, i) in targets"
      :key="t.key"
      class="target-row"
      :class="{ live: t.active, failed: t.failed }"
      :style="{ top: rowY(i) - 23 + 'px' }"
    >
      <div class="target-account">
        <span class="target-dot" /><img :src="iconFor(t.provider)" alt="" /><b
          :title="t.account"
          >{{ t.account }}</b
        >
      </div>
      <div class="target-meta">
        <code :title="`${t.provider}/${t.model}`">{{ t.model }}</code
        ><span
          >{{ state(t)
          }}<template v-if="t.active > 1"> ×{{ t.active }}</template></span
        >
      </div>
    </div>
    <div
      v-if="!targets.length"
      class="target-row empty-target"
      :style="{ top: mid - 23 + 'px' }"
    >
      等待路由数据
    </div>
    <div
      v-for="p in playing"
      :key="p.event.key"
      class="trace-bird"
      :class="{
        failed: p.event.failed,
        returning: p.event.kind === 'response' || p.event.kind === 'result',
      }"
      :style="birdStyle(p)"
    >
      <FlyingMagpie />
    </div>
    <small v-if="(data.targets?.length || 0) > 4" class="more-targets"
      >另 {{ (data.targets?.length || 0) - 4 }} 个目的地 · 控制台查看</small
    >
  </div>
</template>
