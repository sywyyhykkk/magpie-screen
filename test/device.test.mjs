import { test } from 'node:test';
import assert from 'node:assert/strict';
import { defaults, validateConfig, quotaSelection } from '../device.mjs';
const accounts = [{ id: 'one', provider: 'codex', windows: [{ id: 'hour', name: 'hour', remaining: 0 }, { id: 'week', name: 'week', remaining: 80 }] }, { id: 'two', provider: 'claude', windows: [{ id: 'hour', remaining: 50 }] }];
test('device selection preserves zero and never substitutes an unavailable explicit account/window', () => {
  assert.equal(quotaSelection(accounts, defaults).remaining, 0);
  assert.equal(quotaSelection(accounts, { ...defaults, windowId: 'week' }).remaining, 80);
  assert.equal(quotaSelection(accounts, { ...defaults, accountId: 'missing' }).remaining, null);
  assert.equal(quotaSelection(accounts, { ...defaults, windowId: 'missing' }).remaining, null);
  assert.equal(quotaSelection(accounts, { ...defaults, accountId: 'two' }).remaining, null);
  assert.equal(quotaSelection([], defaults).remaining, null);
});
test('config rejects invalid ranges and non-device network destinations', () => {
  for (const bad of [{ screen: 0 }, { brightness: 256 }, { viscosity: NaN }, { deviceHost: 'example.com' }, { deviceHost: '127.0.0.1:3425' }, { background: 'yes' }, { extra: 1 }]) assert.throws(() => validateConfig({ ...defaults, ...bad }));
  assert.equal(validateConfig({ ...defaults, deviceHost: '192.168.1.50' }).deviceHost, '192.168.1.50');
});
