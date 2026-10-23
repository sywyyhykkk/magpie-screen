import http from "node:http";
import { readFile } from "node:fs/promises";
import { resolve, extname } from "node:path";
import { fileURLToPath } from "node:url";
import { randomUUID } from "node:crypto";
const root = fileURLToPath(new URL(".", import.meta.url));
const safe = (v) =>
  typeof v === "string" && /^[\w./:+ -]{1,100}$/.test(v) ? v : "";
export function sanitizeRoute(r, account) {
  if (!r) return null;
  const tries = Array.isArray(r.tries) ? r.tries : [];
  return {
    id: Number(r.id) || 0,
    time: typeof r.time === "string" ? r.time : "",
    kind: safe(r.kind),
    asked: safe(r.asked),
    model: safe(r.model),
    effort: safe(r.effort),
    done: r.done === true,
    status: Number(r.status) || 0,
    failed: !!r.error,
    ms: Number(r.ms) || 0,
    tries: tries.length,
    account: account || null,
  };
}
// Trace responses are reduced here; prompt, folder, raw seat IDs and errors never leave the proxy.
export function sanitizeTraceRoute(r, accountLabel) {
  const seats = [...(r.order || []), ...(r.left || [])];
  const targets = (r.tries || []).map((t, index) => {
    const seat = seats.find((s) => s.id === t.id);
    const provider = safe(seat?.provider || r.provider);
    const model = safe(t.model || seat?.model || r.model);
    const account = accountLabel(seat);
    return {
      key: `${account.key}:${provider}/${model}`,
      account: account.label,
      provider,
      model,
      effort: safe(t.effort),
      done: t.done === true,
      status: Number(t.status) || 0,
      failed: !!t.fail || Number(t.status) >= 400,
      index,
      responseSeen:
        Number(t.ttft) > 0 ||
        Number(t.firstText) > 0 ||
        Number(t.firstReasoning) > 0,
    };
  });
  return {
    ...sanitizeRoute(r),
    agent: safe(r.agent),
    model: targets.length
      ? `${targets.at(-1).provider}/${targets.at(-1).model}`
      : safe(r.model),
    effort: targets.at(-1)?.effort || safe(r.effort),
    account: targets.at(-1)?.account || null,
    targets,
  };
}
const numeric = (value) =>
  typeof value === "number" && Number.isFinite(value) && value >= 0
    ? value
    : null;
export function sanitizeUsage(j) {
  if (!j || numeric(j.calls) === null || !Array.isArray(j.models))
    throw Error("usage schema");
  const metrics = (row) => {
    const input = numeric(row.input),
      output = numeric(row.output);
    return {
      calls: numeric(row.calls),
      errors: numeric(row.errors),
      input,
      output,
      tokens: input === null || output === null ? null : input + output,
      cacheRead: numeric(row.cache_read),
      cost: numeric(row.cost),
      unpriced: numeric(row.unpriced),
    };
  };
  const grouped = new Map();
  for (const model of j.models) {
    const id = safe(model.provider) || "session-unknown";
    const next = metrics(model);
    if (!grouped.has(id)) grouped.set(id, { id, ...next });
    else {
      const group = grouped.get(id);
      for (const key of Object.keys(next))
        group[key] =
          group[key] === null || next[key] === null
            ? null
            : group[key] + next[key];
    }
  }
  const providers = [...grouped.values()].sort(
    (a, b) => (b.tokens ?? 0) - (a.tokens ?? 0),
  );
  const optional = (key) => (j[key] === undefined ? 0 : numeric(j[key]));
  return {
    period: "today",
    ...metrics(j),
    providers,
    cacheWrite: numeric(j.cache_write),
    retried: optional("retried"),
    timed: optional("timed"),
    ttftMs: optional("ttft_ms"),
    decodeMs: optional("decode_ms"),
    decodeOut: optional("decode_out"),
    decodeN: optional("decode_n"),
    floorMs: optional("floor_ms"),
    floorOut: optional("floor_out"),
    floorN: optional("floor_n"),
  };
}
export function createApi(fetcher = fetch) {
  const sessions = new Map(),
    reverse = new Map(),
    accounts = new Map(),
    matches = new Map();
  const alias = (map, key, prefix) => {
    if (!map.has(key))
      map.set(key, `${prefix} ${String(map.size + 1).padStart(2, "0")}`);
    return map.get(key);
  };
  return async function (req, res) {
    const u = new URL(req.url, "http://127.0.0.1");
    if (!u.pathname.startsWith("/api/")) return false;
    res.setHeader("Cache-Control", "no-store");
    res.setHeader("Content-Type", "application/json");
    const send = (code, data) => {
      res.writeHead(code);
      res.end(JSON.stringify(data));
    };
    if (req.method !== "GET") {
      send(405, { error: "只支持只读请求" });
      return true;
    }
    // No cross-origin access, arbitrary upstream, credentials or request bodies.
    if (
      req.headers.origin &&
      !["http://127.0.0.1:4173", "http://localhost:4173"].includes(
        req.headers.origin,
      )
    ) {
      send(403, { error: "来源不允许" });
      return true;
    }
    const controller = new AbortController();
    res.on("close", () => controller.abort());
    const upstream = async (path) => {
      const r = await fetcher("http://127.0.0.1:3425" + path, {
        signal: AbortSignal.any([
          controller.signal,
          AbortSignal.timeout(30000),
        ]),
        redirect: "error",
      });
      if (!r.ok) throw Error("upstream");
      return r.json();
    };
    try {
      if (u.pathname === "/api/health") {
        await upstream("/api/hello");
        send(200, { connected: true, at: new Date().toISOString() });
      } else if (u.pathname === "/api/usage") {
        const j = await upstream("/v1/magpie/usage?period=today");
        send(200, { ...sanitizeUsage(j), at: new Date().toISOString() });
      } else if (u.pathname === "/api/sessions") {
        const j = await upstream(
          "/v1/magpie/usage/requests?period=today&limit=100",
        );
        if (!Array.isArray(j.rows)) throw Error("schema");
        const result = new Map();
        matches.clear();
        for (const row of j.rows) {
          if (
            typeof row.session !== "string" ||
            !row.session ||
            row.agent !== "codex"
          )
            continue;
          if (!reverse.has(row.session)) {
            const token = randomUUID();
            reverse.set(row.session, token);
            sessions.set(token, row.session);
          }
          const token = reverse.get(row.session);
          if (row.providerAccount && row.route_id)
            matches.set(
              `${row.session}:${row.route_id}`,
              alias(accounts, `${row.provider}:${row.providerAccount}`, "账号"),
            );
          if (u.searchParams.get("background") !== "1" && row.kind) continue;
          if (!result.has(token))
            result.set(token, {
              token,
              label: `会话 ${String([...sessions.keys()].indexOf(token) + 1).padStart(2, "0")}`,
              model: safe(row.model),
              time: row.t,
              kind: safe(row.kind),
            });
        }
        send(200, {
          sessions: [...result.values()],
          at: new Date().toISOString(),
          scope: "今日最近 100 条 Codex 请求",
        });
      } else if (u.pathname === "/api/trace") {
        const token = u.searchParams.get("session");
        const manual = u.searchParams.get("manual");
        const session = manual || (token ? sessions.get(token) : null);
        if ((token && !session) || (manual && manual.length > 128)) {
          send(400, { error: "会话无效，请重新选择" });
          return true;
        }
        const after = Math.max(0, Number(u.searchParams.get("after")) || 0);
        // Unlike session/route, trace treats any wait value as a fixed 25-second long poll.
        const j = await upstream(
          `/v1/magpie/trace?after=${after}${u.searchParams.get("wait") === "1" ? "&wait=1" : ""}`,
        );
        if (
          !Array.isArray(j.routes) ||
          !Array.isArray(j.ids) ||
          !Number.isFinite(j.seq)
        )
          throw Error("schema");
        const full = u.searchParams.get("identity") === "full";
        const labelAccount = (seat) => {
          if (!seat) return { key: "unknown", label: "账号未公开" };
          const key = alias(accounts, `${seat.provider}:${seat.id}`, "账号");
          const label =
            full && typeof seat.who === "string"
              ? seat.who.replace(/[\x00-\x1f\x7f]/g, "").slice(0, 160)
              : key;
          return { key, label: label || key };
        };
        const routes = j.routes
          .filter(
            (r) =>
              ["codex", "claude", "cursor", "chatgpt"].includes(r.agent) &&
              (!session || r.session === session) &&
              (u.searchParams.get("background") === "1" || !r.kind),
          )
          .map((r) => sanitizeTraceRoute(r, labelAccount));
        send(200, {
          seq: j.seq,
          pid: Number(j.pid) || 0,
          ids: j.ids.filter(Number.isFinite),
          routes,
          at: new Date().toISOString(),
        });
      } else if (u.pathname === "/api/route") {
        const token = u.searchParams.get("session");
        const manual = u.searchParams.get("manual");
        const session = manual || sessions.get(token);
        if (!session || session.length > 128) {
          send(400, { error: "请选择或输入会话" });
          return true;
        }
        const after = Math.max(0, Number(u.searchParams.get("after")) || 0);
        const wait = Math.min(
          20,
          Math.max(0, Number(u.searchParams.get("wait")) || 0),
        );
        const j = await upstream(
          `/v1/magpie/route?session=${encodeURIComponent(session)}&after=${after}&wait=${wait}`,
        );
        if (!("route" in j) || !Number.isFinite(j.seq)) throw Error("schema");
        send(200, {
          seq: j.seq,
          route: sanitizeRoute(
            j.route,
            matches.get(`${session}:${j.route?.id}`),
          ),
          at: new Date().toISOString(),
        });
      } else send(404, { error: "接口不存在" });
    } catch {
      if (!res.destroyed)
        send(502, {
          error: "Magpie 不可达或响应不兼容",
          at: new Date().toISOString(),
        });
    }
    return true;
  };
}
export async function start() {
  const prod = process.argv.includes("--production");
  const api = createApi();
  const vite = prod
    ? null
    : await (
        await import("vite")
      ).createServer({
        root,
        server: { middlewareMode: true },
        appType: "spa",
      });
  const server = http.createServer(async (req, res) => {
    if (await api(req, res)) return;
    if (vite) {
      vite.middlewares(req, res);
      return;
    }
    try {
      const pathname = decodeURIComponent(
        new URL(req.url, "http://localhost").pathname,
      );
      const path = resolve(root, "dist", "." + pathname);
      if (!path.startsWith(resolve(root, "dist") + "/") && pathname !== "/")
        throw Error();
      const file = extname(path) ? path : resolve(root, "dist/index.html");
      const body = await readFile(file);
      res.setHeader(
        "Content-Type",
        {
          js: "text/javascript",
          css: "text/css",
          html: "text/html",
          svg: "image/svg+xml",
        }[extname(file).slice(1)] || "application/octet-stream",
      );
      res.end(body);
    } catch {
      res.writeHead(404);
      res.end("Not found");
    }
  });
  server.listen(4173, "127.0.0.1", () =>
    console.log("Magpie Screen: http://127.0.0.1:4173"),
  );
  server.on("error", (e) => {
    console.error(e.code);
    process.exitCode = 1;
    vite?.close();
  });
  for (const signal of ["SIGINT", "SIGTERM"])
    process.on(signal, () => {
      server.close();
      vite?.close();
    });
}
if (
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url)
)
  start();
