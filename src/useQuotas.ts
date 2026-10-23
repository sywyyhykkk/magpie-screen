import { computed, onUnmounted, ref, watch, type Ref } from 'vue';
export interface QuotaWindow { id: string; name: string; remaining: number | null; resetsAt: string | null }
export interface QuotaAccount { id: string; provider: string; label: string; plan: string; readAt: string | null; unavailable: boolean; windows: QuotaWindow[] }
export function useQuotas(enabled: Ref<boolean>) {
  const accounts = ref<QuotaAccount[]>([]), accountId = ref(''), windowId = ref('');
  const status = ref('idle'), error = ref(''), fetchedAt = ref('');
  const account = computed(() => accounts.value.find(a => a.id === accountId.value));
  const window = computed(() => account.value?.windows.find(w => w.id === windowId.value));
  const remaining = computed(() => status.value === 'ready' ? window.value?.remaining ?? null : null);
  watch(account, a => {
    if (!a?.windows.some(w => w.id === windowId.value)) windowId.value = a?.windows[0]?.id || '';
  }, { flush: 'sync' });
  let controller: AbortController | undefined;
  let timer: ReturnType<typeof setTimeout> | undefined;
  let generation = 0;
  async function refresh() {
    clearTimeout(timer); controller?.abort();
    const own = ++generation;
    if (!enabled.value) { status.value = 'idle'; return; }
    const c = new AbortController(); controller = c;
    // Keep a successful reading visible during refresh, replace it on failure.
    if (status.value !== 'ready') status.value = 'loading';
    error.value = '';
    try {
      const response = await fetch('/api/quotas', { signal: c.signal, cache: 'no-store' });
      const data = await response.json();
      if (!response.ok) throw Error(data.error || '额度读取失败');
      if (!Array.isArray(data.accounts)) throw Error('额度响应不兼容');
      if (own !== generation) return;
      accounts.value = data.accounts;
      if (!accounts.value.some(a => a.id === accountId.value)) accountId.value = accounts.value[0]?.id || '';
      fetchedAt.value = data.at;
      status.value = 'ready';
    } catch (e) {
      if (c.signal.aborted || own !== generation) return;
      status.value = 'offline'; error.value = e instanceof Error ? e.message : '额度读取失败';
    } finally {
      if (own === generation && enabled.value) timer = setTimeout(refresh, 30000);
    }
  }
  watch(enabled, refresh, { immediate: true });
  onUnmounted(() => { generation++; clearTimeout(timer); controller?.abort(); });
  return { accounts, accountId, windowId, account, window, status, error, remaining, fetchedAt, refresh };
}
