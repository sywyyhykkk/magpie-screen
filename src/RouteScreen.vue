<script setup lang="ts">
import { labels, type ScreenData } from "./types";
import RouteGraph from "./RouteGraph.vue";
import LiquidScreen from "./LiquidScreen.vue";
import OverviewScreen from "./OverviewScreen.vue";
import { type UsageData } from "./usage";
defineProps<{
  data: ScreenData;
  angle: number;
  upright: number;
  remaining: number | null;
  quotaCaption: string;
  viscosity: number;
  provider: string;
  speed?: number;
  mode: string;
  view: string;
  connected: string;
  usage: UsageData | null;
  usageStatus: string;
  batteryPercent: number | null;
  batteryCharging: boolean;
}>();
</script>
<template>
  <div
    class="screen canvas-only"
    :class="data.state"
    role="group"
    :aria-label="view === 'liquid' ? '供应商余量' : view === 'route' ? labels[data.state] : '今日概览'"
  >
    <Transition name="terminal-page" mode="out-in">
      <div :key="view" class="screen-page">
    <div v-if="view === 'route'" class="orientation-layer" :style="{ transform: `rotate(${upright}deg)` }">
      <div class="route-terminal-heading">
        <code>实时路由</code><span>{{ mode === 'sim' ? '模拟' : '实时' }}</span>
      </div>
      <RouteGraph :data="data" :speed="speed" />

    </div>
    <LiquidScreen v-else-if="view === 'liquid'" :angle="angle" :upright="upright" :remaining="remaining" :caption="quotaCaption" :viscosity="viscosity" :provider="provider" />
    <div v-else class="orientation-layer" :style="{ transform: `rotate(${upright}deg)` }">
      <OverviewScreen :data="usage" :status="usageStatus" :mode="mode" :battery-percent="batteryPercent" :battery-charging="batteryCharging" />
    </div>
      </div>
    </Transition>
  </div>
</template>
