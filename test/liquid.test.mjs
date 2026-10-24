import { test } from 'node:test';
import assert from 'node:assert/strict';
import { liquidShape, polygonArea, MODES, Slosh } from '../src/liquid.ts';

const wavy = Array.from({ length: MODES }, (_, n) => (n % 3 - 1) * 9 / (n + 1));

test('liquid keeps the requested volume during tilted, inverted and wavy states', () => {
  for (const angle of [0, .3, Math.PI / 4, Math.PI / 2, Math.PI, -Math.PI * 1.7]) {
    for (const fraction of [0, .01, .2, .5, .8, .99, 1]) {
      for (const amp of [undefined, wavy]) {
        const { polygon } = liquidShape(angle, fraction, amp);
        assert.ok(Math.abs(polygonArea(polygon) / (480 * 480) - fraction) < .0001);
        for (const p of polygon) assert.ok(Math.abs(p.x) <= 240.00001 && Math.abs(p.y) <= 240.00001);
      }
    }
  }
});

test('shaking stays bounded and the surface settles flat again', () => {
  const fluid = new Slosh(0);
  for (let i = 0; i < 6000; i++) {
    fluid.step(160 * Math.sin(i * .017) + 15 * Math.sin(i * .9), .45, .65, 1 / 60);
    assert.ok(Number.isFinite(fluid.angle));
    for (const a of fluid.amp) assert.ok(Number.isFinite(a) && Math.abs(a) < 80);
  }
  for (let i = 0; i < 3600; i++) fluid.step(0, .45, .65, 1 / 60);
  assert.ok(fluid.settled(0));
});

test('a splash disturbs the surface without changing the volume', () => {
  const fluid = new Slosh(0);
  fluid.splash(220);
  for (let i = 0; i < 6; i++) fluid.step(0, .45, .4, 1 / 60);
  const { polygon, surface } = liquidShape(fluid.angle, .4, fluid.amp);
  const ys = surface.map(p => p.y);
  assert.ok(Math.max(...ys) - Math.min(...ys) > 2);
  assert.ok(Math.abs(polygonArea(polygon) / (480 * 480) - .4) < .0001);
});

test('thicker liquid comes to rest sooner', () => {
  const settle = (viscosity) => {
    const fluid = new Slosh(0);
    for (let i = 0; i < 30; i++) fluid.step(-i * 1.72, viscosity, .5, 1 / 60);
    for (let i = 0; i < 6000; i++) {
      if (fluid.settled(-51.6)) return i;
      fluid.step(-51.6, viscosity, .5, 1 / 60);
    }
    return Infinity;
  };
  const thin = settle(0), medium = settle(.45), thick = settle(1);
  assert.ok(thick < medium && medium < thin && thin < 3600, `${thin} ${medium} ${thick}`);
});
