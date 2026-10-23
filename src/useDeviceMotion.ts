import { ref, onMounted, onUnmounted, watch, type Ref } from 'vue';
export function useDeviceMotion(target: Ref<number>, enabled: Ref<boolean>) {
  const angle = ref(target.value), upright = ref(0), moving = ref(false);
  let frame = 0, previous = 0, settled = 0;
  function tick(now: number) {
    const dt = Math.min((now - (previous || now)) / 1000, .033);
    previous = now;
    const before = angle.value;
    angle.value += (target.value - angle.value) * (1 - Math.exp(-16 * dt));
    if (Math.abs(target.value - angle.value) < .03) angle.value = target.value;
    moving.value = Math.abs(angle.value - before) / Math.max(dt, .001) > 2;
    settled = moving.value ? 0 : settled + dt;
    if (enabled.value && settled > .32) {
      // Hysteresis prevents tiny adjustments near 45° from flipping repeatedly.
      if (Math.abs(angle.value + upright.value) > 52)
        upright.value = -Math.round(angle.value / 90) * 90;
    }
    frame = requestAnimationFrame(tick);
  }
  watch(enabled, value => { if (!value) upright.value = 0; settled = 0; });
  onMounted(() => { frame = requestAnimationFrame(tick); });
  onUnmounted(() => cancelAnimationFrame(frame));
  return { angle, upright, moving };
}
