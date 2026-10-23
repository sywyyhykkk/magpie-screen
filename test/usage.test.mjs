import { test } from "node:test";
import assert from "node:assert/strict";
import { sanitizeUsage } from "../server.mjs";
import { demoUsage } from "../src/usage.ts";
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

test("overview keeps three providers, then merges all but the top two without mutating input", async () => {
  const { overviewProviders } = await import("../src/usage.ts");
  const providers = [10, 40, 20, 30, 5].map((tokens, i) => ({ ...demoUsage.providers[0], id: `p${i}`, tokens }));
  const rows = overviewProviders(providers);
  assert.deepEqual(rows.map((p) => [p.label, p.tokens]), [["p1", 40], ["p3", 30], ["其他", 35]]);
  assert.equal(rows[2].title, "其他 3 个供应商：p2、p0、p4");
  assert.deepEqual(providers.map((p) => p.tokens), [10, 40, 20, 30, 5]);
  assert.equal(overviewProviders(providers.slice(0, 3)).length, 3);
  assert.ok(overviewProviders(providers.slice(0, 3)).every((p) => p.label !== "其他"));
  assert.deepEqual(overviewProviders([]), []);
  assert.equal(overviewProviders([...providers, { ...providers[0], id: "unknown", tokens: null }])[2].tokens, null);
});
