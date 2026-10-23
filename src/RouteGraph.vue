<script setup lang="ts">
import { computed } from "vue";
import type { ScreenData, ScreenTarget } from "./types";
import FlyingMagpie from "./FlyingMagpie.vue";
import magpie from "./assets/magpie.svg";
import chatgpt from "./assets/chatgpt.svg";
import claude from "./assets/claude.svg";
import cursor from "./assets/cursor.svg";
const props = defineProps<{ data: ScreenData }>();
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
const flights = computed(() => {
  const allowed = new Set(targets.value.map((t) => t.key));
  return (props.data.flights || [])
    .filter((f) => allowed.has(f.target))
    .slice(0, 8);
});
const pathFor = (key: string) => {
  const index = targets.value.findIndex((t) => t.key === key);
  return `M190 ${mid.value} C204 ${mid.value} 204 ${rowY(index)} 218 ${rowY(index)}`;
};
const birdStyle = (path: string, i: number) => ({
  offsetPath: `path('${path}')`,
  animationDelay: `${-i * 0.53}s`,
});
const state = (t: ScreenTarget) =>
  t.active
    ? "未完成"
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
    <template v-for="(f, i) in flights" :key="f.key"
      ><div class="trace-bird" :style="birdStyle(`M80 ${mid}H110`, i)">
        <FlyingMagpie />
      </div>
      <div class="trace-bird" :style="birdStyle(pathFor(f.target), i + 1)">
        <FlyingMagpie /></div
    ></template>
    <small v-if="(data.targets?.length || 0) > 4" class="more-targets"
      >另 {{ (data.targets?.length || 0) - 4 }} 个目的地 · 控制台查看</small
    >
  </div>
</template>
