<script setup lang="ts">
import { labels, type ScreenData } from "./types";
import FlyingMagpie from "./FlyingMagpie.vue";
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
    :class="data.state"
    role="img"
    :aria-label="`${mode === 'live' ? '真实' : '模拟'}：${labels[data.state]}，${data.model}`"
  >
    <div class="screen-top">
      <span class="tiny-dot"></span
      >{{ mode === "live" ? "LIVE · 真实数据" : "DEMO · 模拟预览" }}
    </div>
    <template v-if="view === 'route'">
      <div class="screen-heading">每一次请求，都有来路。</div>
      <div class="route-graph">
        <svg class="route-wires" viewBox="0 0 352 100" aria-hidden="true">
          <path d="M80 50H136M216 50H272" />
        </svg>
        <div class="route-node source">
          <img :src="chatgpt" alt="" /><b><i />Codex</b><small>客户端</small>
        </div>
        <div class="route-node hub">
          <img :src="magpie" alt="" /><b>magpie</b><small>127.0.0.1</small>
        </div>
        <div class="route-node destination">
          <span class="account-dot">●</span><b>{{ data.account }}</b
          ><small>实际路由</small>
        </div>
        <div
          v-if="
            data.state === 'pending' ||
            data.state === 'success' ||
            data.state === 'failure'
          "
          :key="data.state"
          class="bird-flight"
        >
          <FlyingMagpie />
        </div>
      </div>
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
