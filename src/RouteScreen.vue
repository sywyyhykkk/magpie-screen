<script setup lang="ts">
import { labels, type ScreenData } from "./types";
import RouteGraph from "./RouteGraph.vue";
import magpie from "./assets/magpie.svg";
defineProps<{
  data: ScreenData;
  speed?: number;
  mode: string;
  view: string;
  connected: string;
}>();
</script>
<template>
  <div
    class="screen canvas-only"
    :class="data.state"
    role="img"
    :aria-label="`${mode === 'live' ? '真实' : '模拟'}：${view === 'route' ? labels[data.state] : connected}，${data.model}`"
  >
    <RouteGraph v-if="view === 'route'" :data="data" :speed="speed" />
    <div
      v-else
      class="gateway-mark"
      :class="{
        online: connected === '已连接' || connected === '模拟连接正常',
      }"
      :aria-label="connected"
    >
      <img :src="magpie" alt="" /><i />
    </div>
  </div>
</template>
