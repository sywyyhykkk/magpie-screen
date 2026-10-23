import { test } from "node:test";
import assert from "node:assert/strict";
import { routeEvents } from "../src/types.ts";
const t = (index, extra = {}) => ({
  index,
  key: "account/model-" + index,
  done: false,
  responseSeen: false,
  failed: false,
  ...extra,
});
const r = (id, targets = [t(0)], extra = {}) => ({
  id,
  targets,
  done: false,
  status: 0,
  failed: false,
  ...extra,
});
const map = (...rows) => new Map(rows.map((x) => [x.id, x]));
test("initial pending or completed snapshots never replay history", () => {
  assert.deepEqual(routeEvents(map(), [r(1)], true), []);
  assert.deepEqual(
    routeEvents(
      map(),
      [r(1, [t(0, { done: true })], { done: true, status: 200 })],
      true,
    ),
    [],
  );
});
test("failed attempt returns, new fallback goes out, completion returns once", () => {
  const a = r(1);
  const b = r(1, [t(0, { done: true, failed: true }), t(1)]);
  const changes = routeEvents(map(a), [b], false);
  assert.deepEqual(
    changes.map((e) => e.kind),
    ["response", "attempt"],
  );
  assert.equal(changes[0].failed, true);
  assert.equal(changes[1].target, "account/model-1");
  const c = r(1, [t(0, { done: true, failed: true }), t(1, { done: true })], {
    done: true,
    status: 200,
  });
  assert.deepEqual(
    routeEvents(map(b), [c], false).map((e) => e.kind),
    ["response", "result"],
  );
  assert.deepEqual(routeEvents(map(c), [c], false), []);
});
