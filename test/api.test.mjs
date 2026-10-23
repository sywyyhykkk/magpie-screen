import { test } from "node:test";
import assert from "node:assert/strict";
import http from "node:http";
import { createApi } from "../server.mjs";
const row = {
  agent: "codex",
  session: "secret-session",
  model: "gpt-6-astra",
  route_id: 7,
  provider: "codex",
  providerAccount: "private@example.invalid",
  t: "2000-01-01T00:00:00Z",
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
