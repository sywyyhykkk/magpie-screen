<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { liquidShape, Slosh } from './liquid';
import codex from './assets/chatgpt.svg';
import claude from './assets/claude.svg';
import magpie from './assets/magpie.svg';
import { providerName } from './usage';
const props = defineProps<{ angle: number; upright: number; remaining: number | null; caption: string; viscosity: number; provider: string }>();
const canvas = ref<HTMLCanvasElement>();
const isClaude = computed(() => ['claude', 'anthropic'].includes(props.provider));
const isCodex = computed(() => ['codex', 'chatgpt', 'openai'].includes(props.provider));
const name = computed(() => isClaude.value ? 'CLAUDE' : isCodex.value ? 'CODEX' : props.provider ? providerName(props.provider).toUpperCase() : 'MAGPIE');
let frame = 0, previous = 0, fluid: Slosh;
let fill = 0;
// Leave 5px of headroom at full quota; keep the physics and volume solver intact.
const MAX_FILL = (480 - 5) / 480;
function draw(now: number) {
  const ctx = canvas.value?.getContext('2d');
  if (!ctx) return;
  const dt = Math.min((now - (previous || now)) / 1000, 1 / 30);
  previous = now;
  fill += ((props.remaining ?? 0) / 100 - fill) * (1 - Math.exp(-3 * dt));
  if (props.remaining == null) fill = 0;
  const displayFill = fill * MAX_FILL;
  fluid.step(props.angle, props.viscosity, displayFill, dt);
  const { polygon, surface } = liquidShape(fluid.angle, displayFill, fluid.amp);
  ctx.clearRect(0, 0, 480, 480);
  ctx.save();
  ctx.translate(240, 240);
  const down = { x: -Math.sin(fluid.angle), y: Math.cos(fluid.angle) };
  const level = surface.reduce((sum, p) => sum + p.x * down.x + p.y * down.y, 0) / surface.length;
  const gradient = ctx.createLinearGradient(down.x * level, down.y * level, down.x * (level + 430), down.y * (level + 430));
  if (isClaude.value) {
    gradient.addColorStop(0, '#f9ac70e8'); gradient.addColorStop(.3, '#dd793ee8'); gradient.addColorStop(1, '#803c25f5');
  } else {
    gradient.addColorStop(0, '#696969f2'); gradient.addColorStop(.08, '#303030f5'); gradient.addColorStop(.35, '#191919f5'); gradient.addColorStop(.75, '#0c0c0cfa'); gradient.addColorStop(1, '#080808ff');
  }
  if (props.remaining != null && polygon.length) {
    ctx.beginPath();
    polygon.forEach((p, i) => i ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y));
    ctx.closePath(); ctx.fillStyle = gradient; ctx.fill();
    ctx.save();
    ctx.clip();
    // Subsurface light follows the liquid, rather than rotating with the chassis.
    const glow = ctx.createRadialGradient(-100, -100, 10, -70, -40, 330);
    glow.addColorStop(0, isClaude.value ? '#ffd7a735' : '#ffffff20');
    glow.addColorStop(1, '#ffffff00');
    ctx.fillStyle = glow; ctx.fillRect(-240, -240, 480, 480);
    if (displayFill > .001 && displayFill < .999) {
      ctx.beginPath();
      surface.forEach((p, i) => i ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y));
      for (const p of [...surface].reverse()) ctx.lineTo(p.x + down.x * 12, p.y + down.y * 12);
      ctx.closePath();
      ctx.fillStyle = isClaude.value ? '#fff0d125' : '#ffffff24'; ctx.fill();
      ctx.beginPath();
      surface.forEach((p, i) => {
        const x = p.x + down.x * 13, y = p.y + down.y * 13;
        if (i) ctx.lineTo(x, y); else ctx.moveTo(x, y);
      });
      ctx.strokeStyle = isClaude.value ? '#ffe4c32b' : '#ffffff30'; ctx.lineWidth = 1; ctx.stroke();
    }
    ctx.restore();
  }
  if (displayFill > .001 && displayFill < .999) {
    ctx.beginPath();
    surface.forEach((p, i) => i ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y));
    ctx.strokeStyle = isClaude.value ? '#fff0cddd' : '#f5f5f5ee';
    ctx.lineWidth = 1.6; ctx.shadowColor = ctx.strokeStyle; ctx.shadowBlur = 9; ctx.stroke();
    ctx.shadowBlur = 0;
  }
  ctx.restore();
  frame = requestAnimationFrame(draw);
}
// New quota lands as a drop in the middle of the surface.
watch(() => props.remaining, (next, old) => {
  if (fluid && next != null && old != null) fluid.splash(Math.max(-160, Math.min(320, (next - old) * 8)));
});
onMounted(() => {
  fluid = new Slosh(props.angle); fill = (props.remaining ?? 0) / 100;
  frame = requestAnimationFrame(draw);
});
onUnmounted(() => cancelAnimationFrame(frame));
</script>
<template>
  <section class="liquid-screen" :class="{ 'claude-liquid': isClaude }" :aria-label="`供应商余量：${caption}`">
    <canvas ref="canvas" width="480" height="480" aria-hidden="true" />
    <div class="bottle-glass" aria-hidden="true" />
    <div class="orientation-layer liquid-copy" :style="{ transform: `rotate(${upright}deg)` }">
      <div class="liquid-identity"><img :src="isClaude ? claude : isCodex ? codex : magpie" alt="" /><span>{{ name }}</span></div>
      <div class="liquid-number">{{ remaining == null ? '—' : Number(remaining.toFixed(1)) }}<span v-if="remaining != null">%</span></div>
      <div class="liquid-caption">{{ caption }}</div>
    </div>
    <div class="bottle-ticks" aria-hidden="true"><i v-for="n in 9" :key="n" /></div>
  </section>
</template>
