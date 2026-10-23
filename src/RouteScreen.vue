<script setup lang="ts">
import { labels, type ScreenData } from "./types";
import RouteGraph from "./RouteGraph.vue";
import magpie from "./assets/magpie.svg";
import chatgpt from "./assets/chatgpt.svg";
defineProps<{
  data: ScreenData;
  mode: string;
  view: string;
  connected: string;
}>();
</script>
<template>
  <div
    class="screen"
    :class="[data.state, { multi: (data.targets?.length || 0) >= 3 }]"
    role="img"
    :aria-label="`${mode === 'live' ? '真实' : '模拟'}：${labels[data.state]}，${data.model}`"
  >
    <div class="screen-top">
      <span class="tiny-dot"></span
      >{{ mode === "live" ? "LIVE · 真实数据" : "DEMO · 模拟预览" }}
    </div>
    <template v-if="view === 'route'">
      <div class="screen-heading">每一次请求，都有来路。</div>
      <RouteGraph :data="data" />
      <div class="status-title" aria-live="polite">
        <span class="state-dot" />{{ labels[data.state] }}
      </div>
      <div class="model">{{ data.model }}</div>
      <div class="effort">
        {{
          data.effort
            ? data.effort.toUpperCase() + " REASONING"
            : "ROUTE MONITOR"
        }}
      </div>
      <div class="screen-foot">
        {{ data.detail
        }}<span v-if="data.tries"> · {{ data.tries }} 次尝试</span>
      </div>
    </template>
    <template v-else
      ><img class="service-bird" :src="magpie" alt="" />
      <div class="status-title"><span class="state-dot" />{{ connected }}</div>
      <div class="model">Magpie Gateway</div>
      <div class="effort">127.0.0.1 : 3425</div>
      <div class="screen-foot">
        {{ mode === "live" ? "只读连接状态" : "模拟服务状态" }}
      </div></template
    >
    <div class="screen-tabs">
      <b :class="{ on: view === 'route' }"></b
      ><b :class="{ on: view === 'service' }"></b>
    </div>
  </div>
</template>
