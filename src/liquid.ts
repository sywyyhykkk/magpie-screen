// Screen-space fluid surface: conserved area inside the square, damped slosh modes.
export type Point = { x: number; y: number };
const HALF = 240;
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
export function liquidShape(angle: number, fraction: number, wave: number, ripple: number, heights?: ArrayLike<number>) {
  const c = Math.cos(angle), s = Math.sin(angle);
  const rotate = (x: number, y: number): Point => ({ x: x * c - y * s, y: x * s + y * c });
  const make = (level: number) => {
    const surface = Array.from({ length: 65 }, (_, i) => {
      const x = -350 + i * 700 / 64;
      return rotate(x, level + wave * Math.sin(x / 115) + ripple * Math.cos(x / 57) + (heights?.[i] ?? 0));
    });
    return { surface, polygon: clipSquare([...surface, rotate(350, 700), rotate(-350, 700)]) };
  };
  const target = Math.max(0, Math.min(1, fraction)) * 480 * 480;
  let lo = -460, hi = 460;
  for (let i = 0; i < 17; i++) {
    const mid = (lo + hi) / 2;
    if (polygonArea(make(mid).polygon) > target) lo = mid;
    else hi = mid;
  }
  return make((lo + hi) / 2);
}
export class Slosh {
  angle: number;
  velocity = 0;
  wave = 0;
  waveVelocity = 0;
  ripple = 0;
  rippleVelocity = 0;
  // Coupled surface columns carry disturbances away from the walls and reflect them.
  heights = new Float64Array(65);
  speeds = new Float64Array(65);
  private forces = new Float64Array(65);
  lastTarget: number;
  lastSpeed = 0;
  constructor(deviceAngle = 0) {
    this.angle = this.lastTarget = -deviceAngle * Math.PI / 180;
  }
  step(deviceAngle: number, viscosity: number, dt: number) {
    dt = Math.min(Math.max(dt, .001), 1 / 30);
    const target = -deviceAngle * Math.PI / 180;
    const speed = (target - this.lastTarget) / dt;
    const impulse = Math.max(-12, Math.min(12, speed - this.lastSpeed));
    this.lastTarget = target;
    this.lastSpeed = speed;
    this.waveVelocity += impulse * 28;
    this.rippleVelocity += impulse * 7;
    const damping = 2.5 + viscosity * 8;
    const drive = Math.max(-4, Math.min(4, speed));
    for (let i = 0; i < 65; i++) {
      const x = (i - 32) / 32;
      const wall = Math.exp(-(((x - .63) / .13) ** 2)) - Math.exp(-(((x + .63) / .13) ** 2));
      this.speeds[i] += impulse * 30 * wall;
    }
    for (let step = 0; step < 8; step++) {
      const h = dt / 8;
      for (let i = 0; i < 65; i++) {
        const left = Math.max(0, i - 1), right = Math.min(64, i + 1);
        const x = (i - 32) / 32;
        const wall = Math.exp(-(((x - .63) / .13) ** 2)) - Math.exp(-(((x + .63) / .13) ** 2));
        this.forces[i] = 2400 * (this.heights[left] + this.heights[right] - 2 * this.heights[i])
          - 9 * this.heights[i] - (1.2 + viscosity * 3.5) * this.speeds[i]
          + viscosity * 45 * (this.speeds[left] + this.speeds[right] - 2 * this.speeds[i])
          + drive * 700 * wall;
      }
      for (let i = 0; i < 65; i++) {
        this.speeds[i] += this.forces[i] * h;
        this.heights[i] += this.speeds[i] * h;
      }
    }
    // Substeps keep the spring stable on slower frames.
    for (let i = 0; i < 4; i++) {
      const h = dt / 4;
      this.velocity += ((target - this.angle) * 45 - this.velocity * damping) * h;
      this.angle += this.velocity * h;
      this.waveVelocity += (-48 * this.wave - damping * this.waveVelocity) * h;
      this.wave += this.waveVelocity * h;
      this.rippleVelocity += (-110 * this.ripple - (damping + 2) * this.rippleVelocity) * h;
      this.ripple += this.rippleVelocity * h;
    }
  }
}
