<script setup lang="ts">
import { computed } from "vue";
import { compact, providerName, overviewMetrics, type UsageData } from "./usage";
const props = defineProps<{
  data: UsageData | null;
  mode: string;
  status: string;
}>();
const metrics = computed(() => overviewMetrics(props.data));
const number = (n: number | null | undefined) =>
  n == null ? "—" : n.toLocaleString();
const pct = (n: number | null) =>
  n == null ? "—" : (100 * n).toFixed(1) + "%";
const note = computed(() =>
  props.status === "offline"
    ? "数据不可用"
    : props.status === "paused"
      ? "采集已暂停"
      : props.status === "loading"
        ? "正在读取…"
        : props.data?.calls === 0
          ? "今日暂无调用"
          : "今日全部来源",
);
</script>
<template>
  <section class="overview-screen merged-overview" aria-label="数据概览">
    <div class="usage-heading">
      <h2>今日概览</h2>
      <span class="overview-mode">{{
        mode === "sim" ? "模拟" : "实时"
      }}</span>
    </div>
    <div class="overview-token">
      <div class="overview-total-line"><strong :title="number(metrics.totalTokens)">{{ compact(metrics.totalTokens) }}</strong><span>Token · 含缓存</span></div>
      <div class="overview-parts">
        <span>输入 {{ compact(data?.input) }}</span
        ><span>输出 {{ compact(data?.output) }}</span
        ><span>缓存 {{ compact(metrics.cache) }}</span>
      </div>
    </div>
    <div class="overview-grid">
      <div class="overview-card">
        <span class="overview-label">请求</span
        ><strong>{{ number(data?.calls) }}</strong>
        <div class="overview-small">成功 {{ pct(metrics.successRate) }}</div>
      </div>
      <div class="overview-card">
        <span class="overview-label">费用估算 · USD</span
        ><strong class="cost-value">{{
          data?.cost == null ? "—" : "≈$" + data.cost.toFixed(2)
        }}</strong
        ><small v-if="data?.unpriced">{{ number(data.unpriced) }} 次未计价</small>
      </div>
      <div class="overview-card">
        <span class="overview-label">缓存命中率</span
        ><strong>{{ pct(metrics.cacheRate) }}</strong>
        <div class="overview-meter">
          <i
            :style="{
              width:
                (metrics.cacheRate == null
                  ? 0
                  : Math.min(100, metrics.cacheRate * 100)) + '%',
            }"
          />
        </div>
      </div>
      <div class="overview-card speed-card">
        <span class="overview-label">输出速度</span>
        <div class="overview-speed">
          <strong>{{
            metrics.speed == null
              ? "—"
              : (metrics.speedIsFloor ? "≥" : "") + Math.round(metrics.speed)
          }}</strong
          ><span>token/秒</span>
        </div>
        <small v-if="metrics.speedIsFloor">下限估算</small>
      </div>
    </div>
    <div class="overview-providers">
      <div class="usage-provider-title"><b>供应商</b><span>输入＋输出</span></div>
      <div v-for="p in data?.providers.slice(0, 3) || []" :key="p.id" class="compact-provider">
        <span>{{ providerName(p.id) }}</span><div class="provider-track"><i :style="{ width: (data?.tokens && p.tokens != null ? Math.min(100, p.tokens / data.tokens * 100) : 0) + '%' }" /></div><b>{{ compact(p.tokens) }}</b>
      </div>
      <div v-if="!data?.providers.length" class="provider-empty">{{ status === 'offline' ? '供应商数据不可用' : status === 'loading' ? '读取中…' : '暂无供应商用量' }}</div>
    </div>
    <div class="overview-footer">{{ note }}</div>
  </section>
</template>
