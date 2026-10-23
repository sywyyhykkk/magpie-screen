import { test } from "node:test";
import assert from "node:assert/strict";
import { traceToScreen } from "../src/types.ts";
const target = (key, done = false, failed = false) => ({
  key,
  account: "测试账号",
  provider: "codex",
  model: key,
  effort: "high",
  done,
  failed,
  status: done ? (failed ? 500 : 200) : 0,
  index: 0,
});
const route = (id, targets, done = false) => ({
  id,
  targets,
  done,
  agent: "codex",
  model: "codex/test",
  effort: "high",
  account: null,
  status: done ? 200 : 0,
  failed: false,
  tries: targets.length,
  time: "",
  kind: "",
  asked: "",
  ms: 0,
});
test("two concurrent requests keep independent flights even with one shared seat", () => {
  const s = traceToScreen([route(1, [target("a")]), route(2, [target("a")])]);
  assert.equal(s.targets.length, 1);
  assert.equal(s.targets[0].active, 2);
  assert.equal(s.flights.length, 2);
  assert.equal(s.state, "pending");
  assert.notEqual(s.flights[0].key, s.flights[1].key);
});
