<script setup lang="ts">
import { computed } from "vue";
import { compact, overviewProviders, overviewMetrics, type UsageData } from "./usage";
const props = defineProps<{
  data: UsageData | null;
  mode: string;
  status: string;
  batteryPercent: number | null;
  batteryCharging: boolean;
}>();
const providers = computed(() => overviewProviders(props.data?.providers || []));
const displayCount = (n: number | null | undefined) => n != null && n >= 100000 ? compact(n) : number(n);
const metrics = computed(() => overviewMetrics(props.data));
const speedLabel = computed(() => {
  const speed = metrics.value.speed;
  return speed == null ? '—' : (metrics.value.speedIsFloor ? '≥' : '')
    + (speed >= 1000 ? compact(speed) : Math.round(speed));
});
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
      <span class="overview-battery" :class="{ low: batteryPercent != null && batteryPercent <= 10, charging: batteryCharging }" :aria-label="(batteryPercent == null ? '设备电量未知' : `设备电量 ${batteryPercent}%`) + (batteryCharging ? '，充电中' : '')" :title="batteryCharging ? '充电中' : batteryPercent == null ? '设备未连接、未安装电池或电量暂不可用' : '设备当前电量'">
        <svg viewBox="0 0 31 18" aria-hidden="true"><rect x="1" y="1" width="26" height="16" rx="2" fill="none" stroke="currentColor" stroke-width="1.5"/><path d="M28 6h3v6h-3z" fill="currentColor"/><path v-if="batteryCharging" d="M17 3 10 10h5l-3 5 8-8h-5z" fill="currentColor"/><rect v-else-if="batteryPercent != null" x="4" y="4" :width="20 * batteryPercent / 100" height="10" rx=".5" fill="currentColor"/></svg>
        <span>{{ batteryPercent == null ? '—' : batteryPercent }}%</span>
      </span>
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
        ><strong :title="number(data?.calls)">{{ displayCount(data?.calls) }}</strong>
        <div class="overview-small">成功 {{ pct(metrics.successRate) }}</div>
      </div>
      <div class="overview-card">
        <span class="overview-label">费用估算 · USD</span
        ><strong class="cost-value" :title="data?.cost == null ? undefined : data.cost.toFixed(2) + ' USD'">{{
          data?.cost == null ? "—" : "≈$" + (data.cost >= 1000 ? compact(data.cost) : data.cost.toFixed(2))
        }}</strong
        ><small v-if="data?.unpriced" :title="number(data.unpriced) + ' 次未计价'">{{ displayCount(data.unpriced) }} 次未计价</small>
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
          <strong :class="{ 'compact-speed': speedLabel.length > 5 }" :title="metrics.speed == null ? undefined : metrics.speed.toLocaleString() + ' token/秒'">{{ speedLabel }}</strong
          ><span>token/秒</span>
        </div>
        <small v-if="metrics.speedIsFloor">下限估算</small>
      </div>
    </div>
    <div class="overview-providers">
      <div class="usage-provider-title"><b>供应商</b><span>输入＋输出</span></div>
      <div v-for="p in providers" :key="p.key" class="compact-provider">
        <span :title="p.title">{{ p.label }}</span><div class="provider-track"><i :style="{ width: (data?.tokens && p.tokens != null ? Math.min(100, p.tokens / data.tokens * 100) : 0) + '%' }" /></div><b :title="number(p.tokens)">{{ compact(p.tokens) }}</b>
      </div>
      <div v-if="!data?.providers.length" class="provider-empty">{{ status === 'offline' ? '供应商数据不可用' : status === 'loading' ? '读取中…' : '暂无供应商用量' }}</div>
    </div>
    <div class="overview-footer">{{ note }}</div>
  </section>
</template>
