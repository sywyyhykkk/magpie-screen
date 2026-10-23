<script setup lang="ts">
import { computed } from "vue";
import { compact, overviewMetrics, type UsageData } from "./usage";
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
          : "今日全部来源 · 自动刷新",
);
</script>
<template>
  <section class="overview-screen" aria-label="数据概览">
    <div class="usage-heading">
      <h2>今日概览</h2>
      <span class="usage-badge" :class="{ demo: mode === 'sim' }">{{
        mode === "sim" ? "模拟数据" : "真实数据"
      }}</span>
    </div>
    <div class="overview-token">
      <span class="overview-label">Token · 含缓存</span
      ><strong :title="number(metrics.totalTokens)">{{
        compact(metrics.totalTokens)
      }}</strong>
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
        <div class="overview-small">
          <span :class="{ bad: !!data?.errors }"
            >{{ number(data?.errors) }} 次失败</span
          ><span>成功 {{ pct(metrics.successRate) }}</span>
        </div>
        <small>{{ number(data?.retried) }} 次重试后成功</small>
      </div>
      <div class="overview-card">
        <span class="overview-label">费用 · USD</span
        ><strong class="cost-value">{{
          data?.cost == null ? "—" : "≈$" + data.cost.toFixed(2)
        }}</strong
        ><small>生效价格 · 估算</small
        ><small v-if="data?.unpriced"
          >{{ number(data.unpriced) }} 次调用未计价</small
        >
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
        <small>提示词中的缓存读取占比</small>
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
        <small
          >{{ number(metrics.speedSamples) }}
          {{ metrics.speedIsFloor ? "次下限估算" : "次有效计时" }}</small
        ><small
          >首字平均
          {{
            metrics.ttft == null ? "—" : metrics.ttft.toFixed(2) + " 秒"
          }}</small
        ><small>{{ number(data?.timed) }} 次首字计时</small>
      </div>
    </div>
    <div class="overview-footer">{{ note }}</div>
  </section>
</template>
