<script setup lang="ts">
import { computed } from "vue";
import type { UsageData } from "./usage";
import { providerName, compact } from "./usage";
import magpie from "./assets/magpie.svg";
import chatgpt from "./assets/chatgpt.svg";
import claude from "./assets/claude.svg";
import cursor from "./assets/cursor.svg";
const props = defineProps<{
  data: UsageData | null;
  mode: string;
  status: string;
}>();
const providers = computed(() => props.data?.providers.slice(0, 3) || []);
const icons: Record<string, string> = {
  codex: chatgpt,
  openai: chatgpt,
  chatgpt,
  claude,
  anthropic: claude,
  cursor,
};
const money = (n: number | null | undefined) =>
  n == null ? "—" : `$${n.toFixed(2)}`;
const share = (n: number | null) =>
  !props.data?.tokens || n === null
    ? 0
    : Math.min(100, (n / props.data.tokens) * 100);
const notice = computed(() =>
  props.status === "offline"
    ? "用量数据不可用"
    : props.status === "paused"
      ? "采集已暂停"
      : props.status === "loading"
        ? "正在读取用量"
        : props.data?.calls === 0
          ? "今日暂无调用"
          : props.data?.unpriced
            ? `${props.data.unpriced} 次调用未计价`
            : "输入 + 输出 · 不含缓存流量",
);
</script>
<template>
  <section class="usage-screen" aria-label="用量与供应商">
    <div class="usage-heading">
      <img :src="magpie" alt="" />
      <h2>今日用量</h2>
      <span class="usage-badge" :class="{ demo: mode === 'sim' }">{{
        mode === "sim" ? "模拟数据" : "真实数据"
      }}</span>
    </div>
    <div class="usage-total">
      <strong>{{ compact(data?.tokens) }}</strong
      ><span>TOKENS</span>
    </div>
    <div class="usage-token-parts">
      <span>输入 {{ compact(data?.input) }}</span
      ><i /><span>输出 {{ compact(data?.output) }}</span>
    </div>
    <div class="usage-metrics">
      <div>
        <span>请求</span><b>{{ compact(data?.calls) }}</b>
      </div>
      <div>
        <span>费用估算 · USD</span><b>{{ money(data?.cost) }}</b>
      </div>
      <div>
        <span>缓存读取</span><b>{{ compact(data?.cacheRead) }}</b>
      </div>
    </div>
    <div class="usage-provider-title"><b>供应商</b><span>Token 分布</span></div>
    <div v-if="!providers.length" class="usage-placeholder">
      {{
        status === "offline"
          ? "无法连接 Magpie"
          : status === "loading"
            ? "正在读取…"
            : status === "paused"
              ? "已暂停"
              : "暂无供应商用量"
      }}
    </div>
    <div v-for="p in providers" :key="p.id" class="usage-provider">
      <div class="provider-avatar">
        <img v-if="icons[p.id]" :src="icons[p.id]" alt="" /><span v-else>{{
          providerName(p.id).slice(0, 1)
        }}</span>
      </div>
      <div class="provider-body">
        <div class="provider-line">
          <b :title="providerName(p.id)">{{ providerName(p.id) }}</b
          ><span>{{ compact(p.tokens) }}</span>
        </div>
        <div class="provider-track">
          <i :style="{ width: share(p.tokens) + '%' }" />
        </div>
      </div>
    </div>
    <div class="usage-footer">
      <span>{{ notice }}</span
      ><span>{{
        data?.providers.length && data.providers.length > 3
          ? `+${data.providers.length - 3} 个`
          : data?.errors == null
            ? ""
            : `错误 ${data.errors}`
      }}</span>
    </div>
  </section>
</template>
