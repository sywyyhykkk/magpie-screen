import { test } from "node:test";
import assert from "node:assert/strict";
import http from "node:http";
import { sanitizeUsage, createApi } from "../server.mjs";
import { compact, demoUsage, emptyUsage } from "../src/usage.ts";
const metric = {
  calls: 2,
  errors: 0,
  input: 100,
  output: 20,
  cache_read: 50,
  cost: 0.1,
  unpriced: 0,
};
const sample = {
  ...metric,
  calls: 6,
  input: 300,
  output: 60,
  path: "/private/config",
  accounts: [{ account: "private@example.invalid" }],
  sessions: [{ id: "private-session" }],
  models: [
    {
      ...metric,
      provider: "codex",
      sub: "private@example.invalid · internal-host",
      host: "internal-host",
    },
    { ...metric, provider: "codex" },
    { ...metric, provider: "claude" },
  ],
};
test("usage groups model totals by provider without duplicating cache tokens or exposing identity", () => {
  const j = sanitizeUsage(sample);
  assert.equal(j.tokens, 360);
  assert.equal(j.providers.length, 2);
  assert.equal(j.providers[0].id, "codex");
  assert.equal(j.providers[0].tokens, 240);
  assert.equal(j.providers[0].calls, 4);
  assert.equal(j.providers[0].cacheRead, 100);
  assert.ok(!JSON.stringify(j).includes("private"));
  assert.ok(!JSON.stringify(j).includes("internal-host"));
  assert.ok(!JSON.stringify(j).includes("@"));
});
test("zero usage is distinct from unavailable or unpriced data", () => {
  const z = sanitizeUsage({
    ...metric,
    calls: 0,
    input: 0,
    output: 0,
    models: [],
  });
  assert.equal(z.tokens, 0);
  assert.equal(z.providers.length, 0);
  const u = sanitizeUsage({
    ...sample,
    cost: undefined,
    output: undefined,
    unpriced: 3,
  });
  assert.equal(u.cost, null);
  assert.equal(u.tokens, null);
  assert.equal(u.unpriced, 3);
  assert.throws(() => sanitizeUsage({}));
  assert.throws(() => sanitizeUsage(null));
});
test("unattributed source is kept separate from known providers", () => {
  const j = sanitizeUsage({
    ...sample,
    models: [
      { ...metric, provider: "" },
      { ...metric, provider: "session-unknown" },
    ],
  });
  assert.equal(j.providers.length, 1);
  assert.equal(j.providers[0].id, "session-unknown");
  assert.equal(j.providers[0].tokens, 240);
});
test("compact formatting preserves missing versus zero; demo totals remain consistent", () => {
  assert.equal(compact(null), "—");
  assert.equal(compact(0), "0");
  assert.equal(compact(1200000), "1.2M");
  assert.equal(
    demoUsage.providers.reduce((n, p) => n + p.tokens, 0),
    demoUsage.tokens,
  );
  assert.equal(emptyUsage.calls, 0);
});
test("usage API reads only the fixed summary endpoint and whitelists its response", async () => {
  let requested = "";
  const api = createApi(async (url) => {
    requested = url;
    return { ok: true, json: async () => sample };
  });
  const s = http.createServer((req, res) => api(req, res));
  await new Promise((r) => s.listen(0, "127.0.0.1", r));
  try {
    const response = await fetch(
      "http://127.0.0.1:" + s.address().port + "/api/usage",
    );
    const j = await response.json();
    assert.equal(response.status, 200);
    assert.equal(
      requested,
      "http://127.0.0.1:3425/v1/magpie/usage?period=today",
    );
    assert.equal(j.period, "today");
    assert.equal(j.providers.length, 2);
    assert.ok(j.at);
    assert.equal(j.path, undefined);
    assert.equal(j.accounts, undefined);
  } finally {
    s.closeAllConnections();
    await new Promise((r) => s.close(r));
  }
});

test("overview matches cache, success and timing formulas without averaging per-call speeds", async () => {
  const { overviewMetrics } = await import("../src/usage.ts");
  const source = {
    ...demoUsage,
    input: 100,
    output: 20,
    cacheRead: 600,
    cacheWrite: 300,
    calls: 20,
    errors: 5,
    timed: 10,
    ttftMs: 5000,
    decodeMs: 2000,
    decodeOut: 100,
    decodeN: 5,
  };
  const m = overviewMetrics(source);
  assert.equal(m.totalTokens, 1020);
  assert.equal(m.cache, 900);
  assert.equal(m.cacheRate, 0.6);
  assert.equal(m.successRate, 0.75);
  assert.equal(m.speed, 50);
  assert.equal(m.ttft, 0.5);
  assert.equal(m.speedSamples, 5);
  assert.equal(m.speedIsFloor, false);
  const floor = overviewMetrics({
    ...source,
    floorN: 10,
    floorMs: 2000,
    floorOut: 60,
  });
  assert.equal(floor.speed, 30);
  assert.equal(floor.speedIsFloor, true);
  assert.equal(floor.speedSamples, 10);
  const tie = overviewMetrics({
    ...source,
    floorN: 5,
    floorMs: 2000,
    floorOut: 60,
  });
  assert.equal(tie.speedIsFloor, false);
});
test("overview keeps missing denominators and absent timing unavailable", async () => {
  const { overviewMetrics } = await import("../src/usage.ts");
  assert.equal(overviewMetrics(null).totalTokens, null);
  const zero = overviewMetrics(emptyUsage);
  assert.equal(zero.totalTokens, 0);
  assert.equal(zero.cacheRate, null);
  assert.equal(zero.successRate, null);
  assert.equal(zero.speed, null);
  assert.equal(zero.ttft, null);
  assert.equal(
    overviewMetrics({ ...demoUsage, cacheWrite: null }).totalTokens,
    null,
  );
});
test("usage API sanitizes timing fields and supplies omitted count zeros", () => {
  const j = sanitizeUsage({
    ...sample,
    cache_write: 40,
    timed: 3,
    ttft_ms: 6000,
    decode_ms: 2000,
    decode_out: 100,
    decode_n: 2,
    retried: 1,
  });
  assert.equal(j.cacheWrite, 40);
  assert.equal(j.ttftMs, 6000);
  assert.equal(j.decodeN, 2);
  assert.equal(j.retried, 1);
  assert.equal(j.floorN, 0);
  assert.equal(sanitizeUsage(sample).timed, 0);
});
