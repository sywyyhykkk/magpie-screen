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
