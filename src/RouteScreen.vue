<script setup lang="ts">
import { labels, type ScreenData } from "./types";
import RouteGraph from "./RouteGraph.vue";
import UsageScreen from "./UsageScreen.vue";
import OverviewScreen from "./OverviewScreen.vue";
import { overviewMetrics, type UsageData } from "./usage";
defineProps<{
  data: ScreenData;
  speed?: number;
  mode: string;
  view: string;
  connected: string;
  usage: UsageData | null;
  usageStatus: string;
}>();
</script>
<template>
  <div
    class="screen canvas-only"
    :class="data.state"
    role="img"
    :aria-label="`${mode === 'live' ? '真实' : '模拟'}：${view === 'route' ? labels[data.state] + '，' + data.model : (view === 'overview' ? '今日概览，' + (overviewMetrics(usage).totalTokens ?? '尚未读取') : '今日用量，' + (usage?.tokens ?? '尚未读取')) + ' tokens'}`"
  >
    <RouteGraph v-if="view === 'route'" :data="data" :speed="speed" />
    <UsageScreen
      v-else-if="view === 'usage'"
      :data="usage"
      :status="usageStatus"
      :mode="mode"
    />
    <OverviewScreen v-else :data="usage" :status="usageStatus" :mode="mode" />
  </div>
</template>
