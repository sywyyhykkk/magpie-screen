// Liquid inside the 480x480 screen, modelled in the gravity frame: x runs along
// the free surface, y points along gravity. Mirrors firmware/src/liquid.h: a bulk
// tilt mode plus standing waves fitted to the real wall-to-wall chord, with
// water-wave dispersion, viscous damping and exact area (volume) conservation.
export type Point = { x: number; y: number };
const HALF = 240, SPAN = 350, GRAVITY = 8000, TENSION = 1.2e5, STEP = 1 / 240;
export const MODES = 32;
const clamp = (v: number, a: number, b: number) => Math.max(a, Math.min(b, v));
const alpha = (dt: number, hz: number) => 1 / (1 + 1 / (2 * Math.PI * hz * dt));
export function polygonArea(points: Point[]) {
  return Math.abs(points.reduce((sum, p, i) => {
    const q = points[(i + 1) % points.length];
    return sum + p.x * q.y - q.x * p.y;
  }, 0)) / 2;
}
function clipSquare(points: Point[]) {
  for (const [axis, sign] of [['x', 1], ['x', -1], ['y', 1], ['y', -1]] as const) {
    const out: Point[] = [];
    for (let i = 0; i < points.length; i++) {
      const a = points[i], b = points[(i + 1) % points.length];
      const av = sign * a[axis] - HALF, bv = sign * b[axis] - HALF;
      if (av <= 0) out.push(a);
      if ((av <= 0) !== (bv <= 0)) {
        const t = av / (av - bv);
        out.push({ x: a.x + t * (b.x - a.x), y: a.y + t * (b.y - a.y) });
      }
    }
    points = out;
  }
  return points;
}
// Flat cut of the square perpendicular to gravity holding `fill` of its area.
export function flatSection(angle: number, fill: number) {
  const c = Math.cos(angle), s = Math.sin(angle);
  const v = [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([X, Y]) => ({ x: X * HALF * c + Y * HALF * s, y: -X * HALF * s + Y * HALF * c }));
  const below = (level: number) => {
    const p: Point[] = [];
    for (let i = 0; i < 4; i++) {
      const a = v[i], b = v[(i + 1) & 3];
      if (a.y >= level) p.push(a);
      if ((a.y >= level) !== (b.y >= level)) {
        const t = (level - a.y) / (b.y - a.y);
        p.push({ x: a.x + t * (b.x - a.x), y: level });
      }
    }
    return p.length ? polygonArea(p) : 0;
  };
  const target = clamp(fill, 0, 1) * 4 * HALF * HALF;
  let lo = Math.min(...v.map(p => p.y)), hi = Math.max(...v.map(p => p.y));
  for (let i = 0; i < 30; i++) {
    const mid = (lo + hi) / 2;
    if (below(mid) > target) lo = mid; else hi = mid;
  }
  const level = (lo + hi) / 2;
  let left = Infinity, right = -Infinity;
  for (let i = 0; i < 4; i++) {
    const a = v[i], b = v[(i + 1) & 3];
    if ((a.y - level) * (b.y - level) <= 0 && a.y !== b.y) {
      const x = a.x + (level - a.y) / (b.y - a.y) * (b.x - a.x);
      left = Math.min(left, x); right = Math.max(right, x);
    }
  }
  if (left > right) left = right = 0;
  return { level, left, right };
}
// Rotation rate and angular acceleration of the gravity direction, which drive
// overshoot at the walls and the centrifugal dish.
export class Motion {
  spin = 0;
  accel = 0;
  private last: number | null = null;
  follow(target: number, dt: number) {
    dt = clamp(dt, .001, .1);
    const last = this.last ?? target, lastSpin = this.spin;
    this.last = target;
    this.spin += ((target - last) / dt - this.spin) * alpha(dt, 10);
    this.accel += (clamp((this.spin - lastSpin) / dt, -400, 400) - this.accel) * alpha(dt, 8);
  }
}
export class Slosh {
  // Bulk orientation of the surface (display radians) and its rate.
  angle: number;
  velocity = 0;
  // Standing-wave amplitudes (px) and rates on the current chord: cos(n pi u).
  amp = new Float64Array(MODES);
  rate = new Float64Array(MODES);
  motion = new Motion();
  constructor(deviceAngle = 0) {
    this.angle = -deviceAngle * Math.PI / 180;
  }
  // Velocity impulse (px/s) of a Gaussian bump at chord position `at` (0..1).
  kick(strength: number, at: number, width: number) {
    const base = 2 * width * Math.sqrt(2 * Math.PI) * strength;
    for (let n = 1; n <= MODES; n++) {
      const k = n * Math.PI * width;
      this.rate[n - 1] += base * Math.cos(n * Math.PI * at) * Math.exp(-.5 * k * k);
    }
  }
  // A drop landing mid-surface, e.g. when quota arrives.
  splash(strength: number) { this.kick(strength, .5, .07); }
  step(deviceAngle: number, viscosity: number, fill: number, dt: number) {
    dt = clamp(dt, 0, .1);
    if (dt <= 0) return;
    viscosity = clamp(viscosity, 0, 1);
    fill = clamp(fill, 0, 1);
    const target = -deviceAngle * Math.PI / 180;
    this.motion.follow(target, dt);
    const { spin, accel } = this.motion;
    const sec = flatSection(this.angle, fill);
    const width = Math.max(sec.right - sec.left, 24), depth = Math.max(fill * 4 * HALF * HALF / width, 4),
      g = GRAVITY, mid = (sec.left + sec.right) / 2;
    // Bulk slosh tempo is the fundamental mode of this chord and depth.
    const k1 = Math.PI / width, bulk = Math.max(12, g * k1 * Math.tanh(k1 * depth) * 1.2),
      zeta = .1 + .85 * viscosity, friction = .35 + 1.6 * viscosity, nu = 6 + 750 * viscosity * viscosity;
    // Thin liquid keeps its world orientation while the container turns; thick
    // liquid is dragged along and lags.
    const inertia = .95 * (1 - viscosity);
    // Walls at the surface height push the liquid towards the low side while
    // turning (overshoot); centrifugal load dishes it towards the walls.
    const turn = clamp(accel, -400, 400) * (1 - .6 * viscosity), lever = HALF / 2 - sec.level,
      spin2 = Math.min(spin * spin, 400), tilt = (turn * lever - spin2 * mid) / g * width,
      dish = -spin2 / (2 * g) * width * width;
    // When gravity swings faster than the bulk follows, the shorter waves
    // already lean towards the new level; thick liquid resists.
    const lean = Math.tan(clamp(target - this.angle, -.6, .6)) * width * (1 - .5 * viscosity);
    const omega2 = new Float64Array(MODES), decay = new Float64Array(MODES), equilibrium = new Float64Array(MODES), limit = new Float64Array(MODES);
    const active = Math.min(MODES, Math.max(1, Math.floor(width / 7)));
    const steps = Math.max(1, Math.ceil(dt / STEP - 1e-4)), h = dt / steps, wallWidth = .05;
    for (let n = 1; n <= MODES; n++) {
      const k = n * Math.PI / width, bound = 1.4 / STEP;
      omega2[n - 1] = Math.min((g * k + TENSION * k ** 3) * Math.tanh(k * depth), bound * bound);
      let damp = friction + 2 * nu * k * k + .05 * Math.sqrt(nu * Math.sqrt(omega2[n - 1]));
      if (n > active) damp += 60;
      decay[n - 1] = Math.exp(-damp * h);
      const shape = 4 / (n * n * Math.PI * Math.PI);
      equilibrium[n - 1] = n & 1 ? -(tilt + (n > 1 ? lean : 0)) * shape : dish * shape;
      limit[n - 1] = .32 / k; // steepness above this breaks
      // Turning jerks push one wall up and pull the other down.
      const kw = n * Math.PI * wallWidth;
      if (n & 1) this.rate[n - 1] += 2 * wallWidth * Math.sqrt(2 * Math.PI) * Math.exp(-.5 * kw * kw) * -turn * lever * .02 * dt;
    }
    for (let i = 0; i < steps; i++) {
      this.velocity += (bulk * (target - this.angle) - 2 * zeta * Math.sqrt(bulk) * this.velocity + inertia * clamp(accel, -400, 400)) * h;
      this.angle += this.velocity * h;
      for (let n = 0; n < MODES; n++) {
        this.rate[n] = (this.rate[n] - omega2[n] * (this.amp[n] - equilibrium[n]) * h) * decay[n];
        this.amp[n] += this.rate[n] * h;
        if (Math.abs(this.amp[n]) > limit[n]) {
          this.amp[n] = Math.sign(this.amp[n]) * limit[n];
          this.rate[n] *= .5;
        }
      }
    }
  }
  settled(deviceAngle: number) {
    if (Math.abs(-deviceAngle * Math.PI / 180 - this.angle) > .002 || Math.abs(this.velocity) > .006) return false;
    return this.amp.every((a, n) => Math.abs(a) <= .08 && Math.abs(this.rate[n]) <= .8);
  }
}
const SAMPLES = 129;
// Surface polyline and liquid polygon in display coordinates; the level is
// solved so the clipped polygon holds exactly `fraction` of the screen.
export function liquidShape(angle: number, fraction: number, amp?: ArrayLike<number>) {
  const c = Math.cos(angle), s = Math.sin(angle);
  const rotate = (x: number, y: number): Point => ({ x: x * c - y * s, y: x * s + y * c });
  fraction = clamp(fraction, 0, 1);
  const sec = flatSection(angle, fraction), width = sec.right - sec.left;
  const active = Math.min(MODES, Math.max(1, Math.floor(width / 7)));
  // Small meniscus where the surface meets the walls.
  const meniscus = 3.5 * clamp(width / 120, 0, 1), reach = 9;
  const xs = new Float64Array(SAMPLES), offset = new Float64Array(SAMPLES);
  for (let i = 0; i < SAMPLES; i++) {
    const u = i / (SAMPLES - 1), x = u * width, wall = Math.min(x, width - x);
    let sum = 0;
    for (let n = 0; n < active; n++) sum += (amp?.[n] ?? 0) * Math.cos((n + 1) * Math.PI * u);
    if (wall < 6 * reach) sum -= meniscus * Math.exp(-wall / reach);
    xs[i] = sec.left + x;
    offset[i] = sum;
  }
  const make = (level: number) => {
    const surface = Array.from(xs, (x, i) => rotate(x, level + offset[i]));
    const ends = [rotate(-SPAN, level + offset[0]), rotate(SPAN, level + offset[SAMPLES - 1])];
    const polygon = clipSquare([ends[0], ...surface, ends[1], rotate(SPAN, 2 * SPAN), rotate(-SPAN, 2 * SPAN)]);
    return { surface, polygon, level };
  };
  const target = fraction * 4 * HALF * HALF;
  if (fraction <= 0 || fraction >= 1) return make(fraction <= 0 ? 2 * SPAN : -2 * SPAN);
  // Bracketed secant: the wetted width is the slope of area against level.
  let lo = sec.level - 2 * HALF, hi = sec.level + 2 * HALF, level = sec.level, shape = make(level);
  for (let i = 0; i < 20; i++) {
    const error = polygonArea(shape.polygon) - target;
    if (Math.abs(error) < .01) break;
    if (error > 0) lo = level; else hi = level;
    const next = width > .5 ? level + error / width : NaN;
    level = next > lo && next < hi ? next : (lo + hi) / 2;
    shape = make(level);
  }
  return shape;
}
