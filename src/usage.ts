export interface UsageMetrics {
  calls: number | null;
  errors: number | null;
  input: number | null;
  output: number | null;
  tokens: number | null;
  cacheRead: number | null;
  cost: number | null;
  unpriced: number | null;
}
export interface ProviderUsage extends UsageMetrics {
  id: string;
}
export interface UsageData extends UsageMetrics {
  period: "today";
  cacheWrite?: number | null;
  retried?: number | null;
  timed?: number | null;
  ttftMs?: number | null;
  decodeMs?: number | null;
  decodeOut?: number | null;
  decodeN?: number | null;
  floorMs?: number | null;
  floorOut?: number | null;
  floorN?: number | null;
  providers: ProviderUsage[];
  at?: string;
}
export function compact(value: number | null | undefined): string {
  if (value == null) return "—";
  if (value >= 1e9) return (value / 1e9).toFixed(1) + "B";
  if (value >= 1e6) return (value / 1e6).toFixed(1) + "M";
  if (value >= 1e3) return (value / 1e3).toFixed(1) + "K";
  return value.toLocaleString();
}
export const providerName = (id: string) =>
  (
    ({
      codex: "Codex",
      openai: "OpenAI",
      chatgpt: "ChatGPT",
      claude: "Claude",
      anthropic: "Anthropic",
      cursor: "Cursor",
      "session-unknown": "未归属 / 本地会话",
    }) as Record<string, string>
  )[id] || id;
export const demoUsage: UsageData = {
  period: "today",
  calls: 1280,
  cacheWrite: 1400000,
  retried: 16,
  timed: 720,
  ttftMs: 3600000,
  decodeMs: 2000000,
  decodeOut: 80000,
  decodeN: 640,
  errors: 8,
  input: 8400000,
  output: 1200000,
  tokens: 9600000,
  cacheRead: 18200000,
  cost: 12.4,
  unpriced: 0,
  providers: [
    {
      id: "codex",
      calls: 1000,
      errors: 5,
      input: 7500000,
      output: 1100000,
      tokens: 8600000,
      cacheRead: 16000000,
      cost: 8,
      unpriced: 0,
    },
    {
      id: "claude",
      calls: 200,
      errors: 2,
      input: 700000,
      output: 100000,
      tokens: 800000,
      cacheRead: 2000000,
      cost: 3,
      unpriced: 0,
    },
    {
      id: "cursor",
      calls: 80,
      errors: 1,
      input: 200000,
      output: 0,
      tokens: 200000,
      cacheRead: 200000,
      cost: 1.4,
      unpriced: 0,
    },
  ],
};
export const emptyUsage: UsageData = {
  cacheWrite: 0,
  retried: 0,
  timed: 0,
  ttftMs: 0,
  decodeMs: 0,
  decodeOut: 0,
  decodeN: 0,
  period: "today",
  calls: 0,
  errors: 0,
  input: 0,
  output: 0,
  tokens: 0,
  cacheRead: 0,
  cost: 0,
  unpriced: 0,
  providers: [],
};

export function overviewMetrics(data: UsageData | null) {
  const sum = (...values: (number | null | undefined)[]) =>
    values.every((v) => v != null)
      ? values.reduce<number>((n, v) => n + v!, 0)
      : null;
  const cache = sum(data?.cacheRead, data?.cacheWrite);
  const totalTokens = sum(data?.input, data?.output, cache);
  const prompt = sum(data?.input, data?.cacheRead, data?.cacheWrite);
  const cacheRate =
    prompt && data?.cacheRead != null ? data.cacheRead / prompt : null;
  const successRate =
    data?.calls && data.errors != null
      ? Math.max(0, 1 - data.errors / data.calls)
      : null;
  const measured =
    data?.decodeMs && data.decodeOut != null && data.decodeN
      ? (data.decodeOut * 1000) / data.decodeMs
      : null;
  const floor =
    data?.floorMs && data.floorOut != null && data.floorN
      ? (data.floorOut * 1000) / data.floorMs
      : null;
  const speedIsFloor = !!(
    floor &&
    (!measured || (data?.floorN ?? 0) > (data?.decodeN ?? 0))
  );
  const speed = speedIsFloor ? floor : measured;
  const speedSamples = speedIsFloor ? data?.floorN : data?.decodeN;
  const otherSpeed =
    measured && floor ? (speedIsFloor ? measured : floor) : null;
  const ttft =
    data?.timed && data.ttftMs != null ? data.ttftMs / data.timed / 1000 : null;
  return {
    cache,
    totalTokens,
    cacheRate,
    successRate,
    speed,
    speedIsFloor,
    speedSamples,
    otherSpeed,
    ttft,
  };
}
