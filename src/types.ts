export type ScreenState =
  | "idle"
  | "pending"
  | "success"
  | "failure"
  | "empty"
  | "offline"
  | "filtered";
export interface Route {
  id: number;
  time: string;
  kind: string;
  asked: string;
  model: string;
  effort: string;
  done: boolean;
  status: number;
  failed: boolean;
  ms: number;
  tries: number;
  account: string | null;
}
export interface ScreenData {
  targets?: ScreenTarget[];
  flights?: ScreenFlight[];
  sources?: string[];
  state: ScreenState;
  model: string;
  effort: string;
  account: string;
  detail: string;
  tries: number;
}
export const labels: Record<ScreenState, string> = {
  idle: "等待请求",
  pending: "请求未完成",
  success: "请求成功",
  failure: "请求失败",
  empty: "没有路由数据",
  offline: "Magpie 未连接",
  filtered: "后台请求已隐藏",
};
export function toScreen(r: Route | null, background: boolean): ScreenData {
  const state: ScreenState = !r
    ? "empty"
    : r.kind && !background
      ? "filtered"
      : !r.done
        ? "pending"
        : r.failed || r.status >= 400
          ? "failure"
          : r.status >= 200 && r.status < 300
            ? "success"
            : "empty";
  return {
    state,
    model: state === "filtered" ? "—" : r?.model || "—",
    effort: r?.effort || "",
    account: state === "filtered" ? "—" : r?.account || "账号未公开",
    detail:
      state === "filtered"
        ? "当前最新记录为后台调用"
        : !r
          ? "该会话尚无可用路由"
          : `HTTP ${r.status || "—"} · ${r.done ? "已结束" : "等待完成状态"}`,
    tries: r?.tries || 0,
  };
}

export interface TraceTarget {
  key: string;
  account: string;
  provider: string;
  model: string;
  effort: string;
  done: boolean;
  status: number;
  failed: boolean;
  index: number;
}
export interface TraceRoute extends Route {
  agent: string;
  targets: TraceTarget[];
}
export interface ScreenTarget extends TraceTarget {
  active: number;
  requests: number;
}
export interface ScreenFlight {
  key: string;
  target: string;
  agent: string;
}
export function traceToScreen(routes: TraceRoute[]): ScreenData {
  const sorted = [...routes].sort((a, b) => b.id - a.id);
  if (!sorted.length)
    return { ...toScreen(null, false), targets: [], flights: [], sources: [] };
  const active = sorted.filter((r) => !r.done);
  const current = active[0] || sorted[0];
  const base = toScreen(current, true);
  const targets = new Map<string, ScreenTarget>();
  const flights: ScreenFlight[] = [];
  // Active routes first, then recent completed routes. A candidate never tried is not a routed destination.
  for (const r of [...active, ...sorted.filter((r) => r.done)]) {
    for (const t of r.targets) {
      if (!targets.has(t.key))
        targets.set(t.key, { ...t, active: 0, requests: 0 });
      const target = targets.get(t.key)!;
      target.requests++;
      if (!r.done && !t.done) {
        target.active++;
        flights.push({
          key: `${r.id}:${t.index}`,
          target: t.key,
          agent: r.agent,
        });
      }
    }
  }
  return {
    ...base,
    detail: active.length
      ? `${active.length} 个请求未完成 · ${flights.length} 个尝试进行中`
      : "最近请求已结束",
    targets: [...targets.values()],
    flights,
    sources: [
      ...new Set(
        (active.length ? active : sorted.slice(0, 1)).map((r) => r.agent),
      ),
    ],
  };
}
