import { test } from 'node:test';
import assert from 'node:assert/strict';
import { liquidShape, polygonArea } from '../src/liquid.ts';

test('liquid keeps the requested volume during tilted, inverted and wavy states', () => {
  for (const angle of [0, .3, Math.PI / 4, Math.PI / 2, Math.PI, -Math.PI * 1.7]) {
    for (const fraction of [0, .01, .2, .5, .8, .99, 1]) {
      const { polygon } = liquidShape(angle, fraction, 22, -9);
      assert.ok(Math.abs(polygonArea(polygon) / (480 * 480) - fraction) < .0001);
      for (const p of polygon) assert.ok(Math.abs(p.x) <= 240.00001 && Math.abs(p.y) <= 240.00001);
    }
  }
});
