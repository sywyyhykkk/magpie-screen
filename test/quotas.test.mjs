import { test } from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { createApi, sanitizeQuotas } from '../server.mjs';
const sample = { data: [
  { provider: 'codex', name: 'Personal', user: 'account-a', kind: 'subscription', readAt: '2026-10-23T12:00:00Z', secret: 'do-not-expose', windows: [
    { name: '5 hours', used: 100, remaining: 0, resetsAt: '2026-10-23T15:00:00Z', secret: 'do-not-expose' },
    { name: '7 days', used: 47, remaining: 53 },
  ] },
  { provider: 'claude', name: 'Work', user: 'account-b', windows: [{ name: 'week', used: 0 }] },
] };
test('quotas preserve account windows, actual zero, and remaining percentage semantics', () => {
  const accounts = sanitizeQuotas(sample, key => key);
  assert.equal(accounts.length, 2);
  assert.equal(accounts[0].windows[0].remaining, 0);
  assert.equal(accounts[0].windows[1].remaining, 53);
  assert.equal(accounts[1].windows[0].remaining, 100);
  assert.equal(accounts[0].readAt, '2026-10-23T12:00:00Z');
  assert.equal(accounts[0].windows[0].resetsAt, '2026-10-23T15:00:00Z');
  assert.ok(!JSON.stringify(accounts).includes('do-not-expose'));
});
test('missing, invalid and failed quota readings stay unknown instead of becoming empty or full', () => {
  const windows = [{}, { remaining: -1 }, { remaining: 101 }, { remaining: '53' }, { used: null }, { remaining: 50, error: 'offline' }];
  const result = sanitizeQuotas({ data: [{ provider: 'codex', windows }, { error: 'offline', windows: [{ remaining: 80 }] }] }, () => 'id');
  assert.ok(result.every(a => a.windows.every(w => w.remaining === null)));
  assert.equal(result[1].unavailable, true);
  assert.throws(() => sanitizeQuotas({}, () => 'id'));
  assert.deepEqual(sanitizeQuotas({ data: [] }, () => 'id'), []);
});
test('quota proxy reads only the fixed endpoint and keeps IDs stable across reordered snapshots', async () => {
  let snapshot = sample, requested;
  const api = createApi(async url => { requested = url; return { ok: true, json: async () => snapshot }; });
  const server = http.createServer((req, res) => api(req, res));
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const url = `http://127.0.0.1:${server.address().port}/api/quotas`;
  try {
    const first = await (await fetch(url)).json();
    assert.equal(requested, 'http://127.0.0.1:3425/v1/magpie/quotas');
    snapshot = { data: [...sample.data].reverse() };
    const next = await (await fetch(url)).json();
    assert.equal(first.accounts[0].id, next.accounts[1].id);
    assert.equal(first.accounts[0].windows[1].id, next.accounts[1].windows[1].id);
    assert.ok(first.at);
    assert.equal((await fetch(url, { method: 'POST' })).status, 405);
    snapshot = {};
    assert.equal((await fetch(url)).status, 502);
  } finally {
    server.closeAllConnections();
    await new Promise(r => server.close(r));
  }
});
