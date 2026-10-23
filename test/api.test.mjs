import { test } from "node:test";
import assert from "node:assert/strict";
import http from "node:http";
import { createApi, sanitizeRoute } from "../server.mjs";
const row = {
  agent: "codex",
  session: "secret-session",
  model: "gpt-6-astra",
  route_id: 7,
  provider: "codex",
  providerAccount: "private@example.invalid",
  t: "2000-01-01T00:00:00Z",
};
const route = {
  id: 7,
  done: true,
  status: 200,
  model: "codex/gpt-6-astra",
  tries: [{}],
  error: "private secret",
  request: "private prompt",
};
async function fixture(fn, fetcher) {
  const api = createApi(fetcher);
  const s = http.createServer((q, r) => api(q, r));
  await new Promise((r) => s.listen(0, "127.0.0.1", r));
  try {
    await fn("http://127.0.0.1:" + s.address().port);
  } finally {
    s.closeAllConnections();
    await new Promise((r) => s.close(r));
  }
}
test("route whitelist removes content and emails", () => {
  const r = sanitizeRoute(route);
  assert.equal(r.failed, true);
  assert.equal(r.model, "codex/gpt-6-astra");
  assert.equal(r.request, undefined);
  assert.equal(r.error, undefined);
  assert.equal(sanitizeRoute(null), null);
});
test("discovery filters side calls, anonymizes and routes exact account", async () =>
  fixture(
    async (base) => {
      const a = await (await fetch(base + "/api/sessions")).json();
      assert.equal(a.sessions.length, 1);
      assert.ok(!JSON.stringify(a).includes("secret-session"));
      assert.ok(!JSON.stringify(a).includes("@"));
      const b = await (
        await fetch(base + "/api/route?session=" + a.sessions[0].token)
      ).json();
      assert.equal(b.route.account, "账号 01");
      assert.equal(b.seq, 4);
      const c = await (await fetch(base + "/api/sessions?background=1")).json();
      assert.equal(c.sessions.length, 2);
      assert.equal(
        (await fetch(base + "/api/route?session=unknown")).status,
        400,
      );
      assert.equal(
        (await fetch(base + "/api/health", { method: "POST" })).status,
        405,
      );
      assert.equal(
        (
          await fetch(base + "/api/sessions", {
            headers: { Origin: "https://example.invalid" },
          })
        ).status,
        403,
      );
    },
    async (url) => ({
      ok: true,
      json: async () =>
        url.includes("usage/requests")
          ? {
              rows: [
                row,
                { ...row, session: "side", kind: "composer_predictions" },
              ],
            }
          : { seq: 4, route },
    }),
  ));
test("null route and unreachable are distinct", async () => {
  await fixture(
    async (b) => {
      const j = await (await fetch(b + "/api/route?manual=missing")).json();
      assert.equal(j.route, null);
    },
    async () => ({ ok: true, json: async () => ({ seq: 0, route: null }) }),
  );
  await fixture(
    async (b) => assert.equal((await fetch(b + "/api/health")).status, 502),
    async () => {
      throw Error("offline");
    },
  );
});
test("disconnect aborts upstream long poll", async () => {
  let aborted = false;
  await fixture(
    async (b) => {
      const c = new AbortController();
      const p = fetch(b + "/api/route?manual=test&wait=20", {
        signal: c.signal,
      }).catch(() => {});
      await new Promise((r) => setTimeout(r, 40));
      c.abort();
      await p;
      await new Promise((r) => setTimeout(r, 40));
      assert.ok(aborted);
    },
    async (_, { signal }) =>
      new Promise((_, reject) =>
        signal.addEventListener(
          "abort",
          () => {
            aborted = true;
            reject(Error("aborted"));
          },
          { once: true },
        ),
      ),
  );
});

test("trace uses real seats, preserves concurrent tries and hides identity by default", async () => {
  const seat = {
    id: "private-seat",
    provider: "codex",
    who: "demo@example.invalid",
    model: "gpt-a",
  };
  const r = {
    id: 8,
    agent: "codex",
    session: "secret-session",
    model: "asked",
    order: [seat],
    tries: [
      { id: seat.id, model: "gpt-a", done: false },
      { id: seat.id, model: "gpt-b", done: false },
    ],
    done: false,
    prompt: "DO NOT LEAK",
    folder: "/private/path",
    error: "private error",
  };
  await fixture(
    async (b) => {
      const a = await (await fetch(b + "/api/trace")).json();
      assert.equal(a.routes.length, 1);
      assert.equal(a.routes[0].targets.length, 2);
      assert.equal(a.routes[0].targets[0].account, "账号 01");
      assert.ok(!JSON.stringify(a).includes("example.invalid"));
      assert.ok(!JSON.stringify(a).includes("private-seat"));
      assert.ok(!JSON.stringify(a).includes("DO NOT LEAK"));
      assert.ok(!JSON.stringify(a).includes("/private/path"));
      assert.ok(!JSON.stringify(a).includes("secret-session"));
      const full = await (await fetch(b + "/api/trace?identity=full")).json();
      assert.equal(full.routes[0].targets[0].account, "demo@example.invalid");
      assert.equal(full.routes[0].targets[1].model, "gpt-b");
      assert.notEqual(
        full.routes[0].targets[0].key,
        full.routes[0].targets[1].key,
      );
      const list = await (await fetch(b + "/api/sessions")).json();
      const scoped = await (
        await fetch(b + "/api/trace?session=" + list.sessions[0].token)
      ).json();
      assert.equal(scoped.routes.length, 1);
      const unknown = await (
        await fetch(b + "/api/trace?manual=missing")
      ).json();
      assert.equal(unknown.routes.length, 0);
      const side = await (await fetch(b + "/api/trace?background=1")).json();
      assert.equal(side.routes.length, 2);
    },
    async (url) => ({
      ok: true,
      json: async () =>
        url.includes("usage/requests")
          ? { rows: [row] }
          : {
              seq: 12,
              pid: 3,
              ids: [8, 9],
              routes: [r, { ...r, id: 9, kind: "composer_predictions" }],
            },
    }),
  );
});

test("trace deltas, process restart metadata and null tries remain explicit", async () => {
  let count = 0;
  await fixture(
    async (b) => {
      const a = await (await fetch(b + "/api/trace")).json();
      assert.equal(a.routes[0].targets.length, 0);
      assert.equal(a.routes[0].done, false);
      const d = await (await fetch(b + "/api/trace?after=3&wait=1")).json();
      assert.equal(d.routes.length, 0);
      assert.equal(d.seq, 3);
    },
    async (url) => {
      assert.ok(!url.includes("/v1/responses"));
      count++;
      return {
        ok: true,
        json: async () => ({
          seq: 3,
          pid: 4,
          ids: [3],
          routes:
            count === 1
              ? [{ id: 3, agent: "codex", tries: [], done: false }]
              : [],
        }),
      };
    },
  );
});
